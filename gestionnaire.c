#include "commun.h"
#include "modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>

static void addLog(SharedData* sharedData, const char* text) {
    if(sharedData->nbLogs < 0)
        sharedData->nbLogs = 0;

    if(sharedData->nbLogs >= MAX_LOGS) {
        sharedData->nbLogs = MAX_LOGS;
        memmove(&sharedData->logs[0], &sharedData->logs[1],
                sizeof(sharedData->logs[0]) * (MAX_LOGS - 1));
        sharedData->nbLogs--;
    }

    snprintf(sharedData->logs[sharedData->nbLogs].message,
             sizeof(sharedData->logs[sharedData->nbLogs].message),
             "%s", text);
    sharedData->nbLogs++;
}

static void notifyParticipants(int msqid, const Meeting* meeting, MsgRequestType type) {
    int count = meeting->nbParticipants;
    if(count < 0)
        count = 0;
    if(count > SIZE_BIGGEST_ROOM)
        count = SIZE_BIGGEST_ROOM;

    for(int i = 0; i < count; i++) {
        Message msg = {0};
        msg.type = meeting->participantIds[i];
        msg.requestType = type;
        msg.meeting = *meeting;
        if(msgsnd(msqid, &msg, MSG_SIZE, IPC_NOWAIT) == -1)
            perror("msgsnd");
    }
}




// Obtenir le pointeur dans la mem partagé du membre avec cet ID
Member* getMemberById(SharedData* s, int idMember) {
    for(int i=0 ; i<NB_EQUIPES ; i++) {
        for(int j=0 ; j<MAX_NB_MEMBRES ; j++) {
            if(s->teams[i].members[j].memberId == idMember) {
                return &s->teams[i].members[j];
            }
        }
    }
    return NULL;
}




// Unbook un meeting, en mettant à jour les calendrier de la salle et des membres 
void unBookMeeting(SharedData* pSharedData, int idMeeting) {
    Meeting* meeting = &pSharedData->meetings[idMeeting];

    Room* room = NULL;
    // Récupérer la room qui était réservée
    for(int i=0 ; i<NB_ROOM ; i++) {
        if(pSharedData->rooms[i].roomId == meeting->roomId) {
            room = &pSharedData->rooms[i];
            break;
        }
    }

    // Mettre le calendrier de la salle pendant les horaires du meetin à FREE
    if(room != NULL) {
        for(int i=meeting->start ; i<meeting->start+meeting->duration ; i++) {
            room->disponibilites[i] = FREE_SLOT;
        }
    }

    // Idem pour le calendrier des membres du meeting
    for(int i=0 ; i<meeting->nbParticipants ; i++) {
        Member* member = getMemberById(pSharedData, meeting->participantIds[i]);
        if(member == NULL) continue;

        for(int j=meeting->start ; j<meeting->start+meeting->duration ; j++) {
            member->disponibilites[j] = FREE_SLOT;
        }
    }
}




// Essaye de book un minor meeting
// Si pas de start spécifié, commencer la recherche à 8H
// Si pas de créneau possible, renvoyer 0. Si ça va, renvoyer 1
int tryBookMinorMeeting(SharedData* pSharedData, int idMeeting) {
    Meeting* meeting = &pSharedData->meetings[idMeeting];
    int firstStart = (meeting->start == -1) ? 0 : meeting->start;

    // Pour chaque room (commencer par la salle A, la plus petite)
    for(int i=0 ; i<NB_ROOM ; i++) {
        Room* room = &pSharedData->rooms[i];

        // Si capacité insuffisante, essayer la prochaine room
        if(room->capacity < meeting->nbParticipants) continue;

        // Essayer de trouver un créneau qui correspond à la durée de la réunion
        for(int start=firstStart ; start<=NB_HOURES_IN_A_DAY-meeting->duration ; start++) {
            int available = 1;

            // Check la dispo de la salle
            for(int j=start ; j<start+meeting->duration ; j++) {
                if(room->disponibilites[j] != FREE_SLOT) {
                    available = 0;
                    break;
                }
            }
            if(!available) continue;

            // Check les dispo des particpants
            for(int j=0 ; j<meeting->nbParticipants ; j++) {
                Member* member = getMemberById(pSharedData, meeting->participantIds[j]);
                if(member == NULL) {
                    available = 0;
                    break;
                }

                for(int k=start ; k<start+meeting->duration ; k++) {
                    if(member->disponibilites[k] != FREE_SLOT) {
                        available = 0;
                        break;
                    }
                }
                if(!available) break;
            }

            // Si tout est okay, on réserve la salle et màj son calendrier ainsi que celui des particpants
            if(available) {

                meeting->start = start;
                meeting->roomId = room->roomId;

                for(int j=start ; j<start+meeting->duration ; j++) {
                    room->disponibilites[j] = meeting->meetingId;
                }

                for(int j=0 ; j<meeting->nbParticipants ; j++) {

                    Member* member = getMemberById(
                        pSharedData,
                        meeting->participantIds[j]
                    );

                    for(int k=start ; k<start+meeting->duration ; k++) {
                        member->disponibilites[k] = meeting->meetingId;
                    }
                }

                return 1;
            }
        }
    }

    return 0;
}



// Essayer de book un major meeting
// Idem que pour minor meeting par rapport au start
/* Si pas de créneaux libre au start souhaité
    --> Si seulement des minors sur le créneaux
        --> Placer la major sur ce créneaux
        --> Déplacer les minors à d'autres moments de la journée (si pas possible, supprimer la/les minors et prévenir les membres)
    --> Si des majors sur le crénaux
        --> Recommencer la recherche à la fin de ce dernier
*/
int tryBookMajorMeeting(int msqid, SharedData* pSharedData, int idMeeting) {
    Meeting* meeting = &pSharedData->meetings[idMeeting];

    if(tryBookMinorMeeting(pSharedData, idMeeting))
        return 1;

    // Si pas de start spécifié, commencer à 8H
    int start = (meeting->start == -1) ? 0 : meeting->start;

    // Pour chaque heure à partir du start...
    while(start <= NB_HOURES_IN_A_DAY-meeting->duration) {
        int restartSearch = 0;

        // Essayer sur chaque room...
        for(int r=0 ; r<NB_ROOM ; r++) {
            Room* room = &pSharedData->rooms[r];

            // Si salle trop petite, essayer la suivante
            if(room->capacity < meeting->nbParticipants) continue;
                
            int meetingsIdToMove[MAX_AMOUNT_MEETING];
            int nbMeetingsToMove = 0;

            int possible = 1;

            // Pour chaque horaire...
            for(int h=start ; h<start+meeting->duration ; h++) {
                // Id du meeting (potentiellement) bloquant
                int blockingMeetingId = room->disponibilites[h];

                // Si le slot est free, passer à l'itération / heure suivante
                if(blockingMeetingId == FREE_SLOT) continue;

                // Récupérer le meeting bloquant
                Meeting* blockingMeeting = &pSharedData->meetings[blockingMeetingId];

                // Si le meeting bloquant est un major, essayer à partir de la fin de ce dernier
                if(blockingMeeting->priorityOrder == MAJOR) {
                    start = blockingMeeting->start + blockingMeeting->duration;
                    possible = 0;
                    restartSearch = 1;
                    break;
                }

                // Vérifier que cette réunion n'est pas déjà dans la liste
                // (une même réunion peut occuper plusieurs slots horaires)
                int alreadyStored = 0;

                for(int m=0 ; m<nbMeetingsToMove ; m++) {
                    // Si déjà enregistrée comme réunion à déplacer
                    if(meetingsIdToMove[m] == blockingMeetingId) {
                        alreadyStored = 1;
                        break;
                    }
                }

                // Si pas encore enregistrée, l'ajouter à la liste
                if(!alreadyStored) {
                    meetingsIdToMove[nbMeetingsToMove++] = blockingMeetingId;
                }
            }

            if(restartSearch)
                break;

            if(!possible)
                continue;

            Meeting meetingsToMove[MAX_AMOUNT_MEETING];

            // Libérer les mineures qui bloquent le créneau de la réunion majeure.
            for(int m=0 ; m<nbMeetingsToMove ; m++) {
                meetingsToMove[m] = pSharedData->meetings[meetingsIdToMove[m]];
                unBookMeeting(pSharedData, meetingsIdToMove[m]);
                pSharedData->meetings[meetingsIdToMove[m]].roomId = NO_ROOM;
            }

            // Puis booker la salle pour la major
            meeting->start = start;
            meeting->roomId = room->roomId;

            for(int h=start ; h<start+meeting->duration ; h++) {
                room->disponibilites[h] = meeting->meetingId;
            }

            // Maj le calendrier de tous les participants
            for(int p=0 ; p<meeting->nbParticipants ; p++) {
                Member* member = getMemberById(pSharedData, meeting->participantIds[p]);

                if(member == NULL) continue;

                for(int h=start ; h<start+meeting->duration ; h++) {
                    member->disponibilites[h] = meeting->meetingId;
                }
            }

            // Replacer les réunions mineures et notifier les participants du résultat.
            for(int m=0 ; m<nbMeetingsToMove ; m++) {
                int movedId = meetingsIdToMove[m];

                if(!tryBookMinorMeeting(pSharedData, movedId)) {
                    notifyMeetingCancelled(msqid, pSharedData, movedId);
                    if(pSharedData->nbMeetings > 0)
                        pSharedData->nbMeetings--;

                    char logMessage[128];
                    snprintf(logMessage, sizeof(logMessage),
                             "Reunion %d annulee (conflit avec une majeure)",
                             movedId);
                    addLog(pSharedData, logMessage);
                }
                else {
                    notifyMeetingMoved(msqid, pSharedData, movedId);

                    char logMessage[128];
                    snprintf(logMessage, sizeof(logMessage),
                             "Reunion %d deplacee de %dH a %dH",
                             movedId,
                             meetingsToMove[m].start + 8,
                             pSharedData->meetings[movedId].start + 8);
                    addLog(pSharedData, logMessage);
                }
            }
            
            return 1;
        }

        if(!restartSearch)
            break;
    }

    return 0;
}







// Notifier les particpants d'une réunion qu'elle a été supprimé (suite à une replanification à cause d'une major)
void notifyMeetingCancelled(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];
    notifyParticipants(msqid, meeting, NOTIFY_MEETING_CANCELLED);
}



// Notifier les particpants d'une réunion qu'elle a été déplacé (suite à une replanification à cause d'une major)
void notifyMeetingMoved(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];
    notifyParticipants(msqid, meeting, NOTIFY_MEETING_MOVED);
}




// Notifier les particpants d'une réunion qu'elle a été déplacé (suite à une demande de booking de réunion d'un membre)
void notifyMeetingAccepted(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];
    notifyParticipants(msqid, meeting, NOTIFY_BOOKING_SUCCESS);
}



// Notifier les participants qu'il n'a pas été possible de booker la réunion
void notifyMeetingRejected(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];
    notifyParticipants(msqid, meeting, NOTIFY_BOOKING_FAILED);
}









/*------------------------- Fonction principale -----------------------------------------*/

void gestionnaire(int msqid, SharedData* sharedData) {
    Message msg;
    while(1) {
        if(msgrcv(msqid, &msg, MSG_SIZE, MANAGER_REQUEST_TYPE, 0) == -1) {
            perror("msgrcv");
            continue;
        }

        if(msg.requestType == DELETE_MEETING) {
            int meetingId = msg.meeting.meetingId;
            if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING ||
               sharedData->meetings[meetingId].roomId == NO_ROOM) {
                addLog(sharedData, "Suppression impossible : reunion introuvable");
                continue;
            }

            Meeting* meeting = &sharedData->meetings[meetingId];
            unBookMeeting(sharedData, meetingId);
            notifyMeetingCancelled(msqid, sharedData, meetingId);
            meeting->roomId = NO_ROOM;
            if(sharedData->nbMeetings > 0)
                sharedData->nbMeetings--;

            char logMessage[128];
            snprintf(logMessage, sizeof(logMessage), "Reunion %d supprimee", meetingId);
            addLog(sharedData, logMessage);
            continue;
        }

        if(msg.requestType == STOP_MANAGER)
            break;

        if(msg.requestType != ASK_REUNION)
            continue;

        if(msg.meeting.nbParticipants < 1 ||
           msg.meeting.nbParticipants > SIZE_BIGGEST_ROOM ||
           (msg.meeting.priorityOrder != MINOR &&
            msg.meeting.priorityOrder != MAJOR) ||
           msg.meeting.duration < 1 ||
           msg.meeting.duration > NB_HOURES_IN_A_DAY ||
           (msg.meeting.start != UNSPECIFIED_START &&
            (msg.meeting.start < 0 ||
             msg.meeting.start > NB_HOURES_IN_A_DAY - msg.meeting.duration))) {
            notifyParticipants(msqid, &msg.meeting, NOTIFY_BOOKING_FAILED);
            addLog(sharedData, "Demande de reunion refusee (parametres invalides)");
            continue;
        }

        if(sharedData->nextMeetingId < 0 ||
           sharedData->nextMeetingId >= MAX_AMOUNT_MEETING) {
            notifyParticipants(msqid, &msg.meeting, NOTIFY_BOOKING_FAILED);
            addLog(sharedData, "Demande de reunion refusee (limite atteinte)");
            continue;
        }

        msg.meeting.meetingId = sharedData->nextMeetingId++;
        msg.meeting.roomId = NO_ROOM;
        sharedData->meetings[msg.meeting.meetingId] = msg.meeting;

        int booked;
        if(msg.meeting.priorityOrder == MINOR)
            booked = tryBookMinorMeeting(sharedData, msg.meeting.meetingId);
        else
            booked = tryBookMajorMeeting(msqid, sharedData, msg.meeting.meetingId);

        char logMessage[128];
        if(booked) {
            notifyMeetingAccepted(msqid, sharedData, msg.meeting.meetingId);
            sharedData->nbMeetings++;
            snprintf(logMessage, sizeof(logMessage),
                     "Reunion %d acceptee en salle %c a %dH",
                     msg.meeting.meetingId,
                     sharedData->meetings[msg.meeting.meetingId].roomId == ROOM_A_ID ? 'A' : 'B',
                     sharedData->meetings[msg.meeting.meetingId].start + 8);
        }
        else {
            notifyMeetingRejected(msqid, sharedData, msg.meeting.meetingId);
            snprintf(logMessage, sizeof(logMessage),
                     "Reunion %d refusee (aucun creneau disponible)",
                     msg.meeting.meetingId);
        }
        addLog(sharedData, logMessage);
    }
}