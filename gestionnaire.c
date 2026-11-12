#include "commun.h"
#include "modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>

// Enregistrer un log dans la mémoire partagée
static void addLog(SharedData* sharedData, const char* text) {
    snprintf(sharedData->logs[sharedData->nbLogs].message,
             sizeof(sharedData->logs[sharedData->nbLogs].message),
             "%s", text);
    sharedData->nbLogs++;
}

// Notifier les participants d'une réunion de son état (acceptée, refusée, déplacée ou annulée)
static void notifyParticipants(int msqid, const Meeting* meeting, MsgRequestType type) {
    int count = meeting->nbParticipants;
    if(count < 0)
        count = 0;
    if(count > SIZE_BIGGEST_ROOM)
        count = SIZE_BIGGEST_ROOM;

    // Envoyer un message à chaque participant
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




// Réserve une réunion mineure dans le premier créneau disponible.
// La recherche commence à l'heure demandée, ou à 8 h si aucune heure n'est précisée.
// Pour chaque heure, les salles sont essayées avant de passer à l'heure suivante.
// Retourne 1 si la réunion est réservée, sinon 0.
int tryBookMinorMeeting(SharedData* pSharedData, int idMeeting) {
    Meeting* meeting = &pSharedData->meetings[idMeeting];
    int firstStart = (meeting->start == -1) ? 0 : meeting->start;

    // Comparer les salles pour chaque heure avant de passer à l'heure suivante.
    for(int start=firstStart ; start<=NB_HOURES_IN_A_DAY-meeting->duration ; start++) {
        for(int i=0 ; i<NB_ROOM ; i++) {
            Room* room = &pSharedData->rooms[i];

            // Si la salle est trop petite, passer à la suivante
            if(room->capacity < meeting->nbParticipants) continue;

            int available = 1;

            // Vérifier que la salle est libre pendant toute la durée demandée.
            for(int j=start ; j<start+meeting->duration ; j++) {
                if(room->disponibilites[j] != FREE_SLOT) {
                    available = 0;
                    break;
                }
            }
            if(!available) continue;

            // Vérifier que chaque participant existe et est libre sur ce créneau.
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

            // Réserver la salle et inscrire la réunion dans les agendas des participants.
            if(available) {
                meeting->start = start;
                meeting->roomId = room->roomId;

                // Mettre à jour le calendrier de la salle
                for(int j=start ; j<start+meeting->duration ; j++) {
                    room->disponibilites[j] = meeting->meetingId;
                }

                // Mettre à jour le calendrier des participants
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


// Ajouter un meetingId à la liste meetingsIdToMove si il n'y est pas déjà
static int addMeetingToMove(int meetingIds[], int* count, int meetingId) {
    for(int i = 0; i < *count; i++) {
        if(meetingIds[i] == meetingId)
            return 1;
    }

    if(*count >= MAX_AMOUNT_MEETING)
        return 0;

    meetingIds[(*count)++] = meetingId;
    return 1;
}


// Réserve une réunion majeure en privilégiant l'heure demandée.
// À chaque heure, essaie les salles dans l'ordre avant d'avancer à l'heure suivante.
// Une réunion majeure déjà planifiée n'est jamais déplacée : la recherche continue
// dans l'autre salle, puis aux heures suivantes.
// Les réunions mineures qui bloquent la salle ou un participant sont libérées,
// puis replacées après la réservation de la majeure. Une mineure impossible à
// replacer est annulée et ses participants sont notifiés.
// Retourne 1 si la majeure est réservée, sinon 0.
int tryBookMajorMeeting(int msqid, SharedData* pSharedData, int idMeeting) {
    Meeting* meeting = &pSharedData->meetings[idMeeting];

    // Utiliser l'heure demandée, ou commencer à 8 h si elle n'est pas précisée.
    int start = (meeting->start == -1) ? 0 : meeting->start;

    // Pour chaque heure, essayer toutes les salles avant de passer à la suivante.
    for(; start <= NB_HOURES_IN_A_DAY-meeting->duration; start++) {
        for(int r=0 ; r<NB_ROOM ; r++) {
            Room* room = &pSharedData->rooms[r];

            // Ignorer les salles dont la capacité est insuffisante.
            if(room->capacity < meeting->nbParticipants) continue;
                
            int meetingsIdToMove[MAX_AMOUNT_MEETING];
            int nbMeetingsToMove = 0;

            int possible = 1;

            for(int h=start ; h<start+meeting->duration ; h++) {
                // Identifier la réunion qui occupe éventuellement cette heure.
                int blockingMeetingId = room->disponibilites[h];

                // Une heure libre ne crée aucun conflit dans cette salle.
                if(blockingMeetingId == FREE_SLOT) continue;
                if(blockingMeetingId < 0 ||
                   blockingMeetingId >= MAX_AMOUNT_MEETING) {
                    possible = 0;
                    break;
                }

                // Une réunion majeure reste prioritaire; essayer un autre créneau.
                Meeting* blockingMeeting = &pSharedData->meetings[blockingMeetingId];

                // Une autre majeure bloque cette salle, mais pas forcément l'autre.
                if(blockingMeeting->priorityOrder == MAJOR) {
                    possible = 0;
                    break;
                }

                if(!addMeetingToMove(meetingsIdToMove, &nbMeetingsToMove,
                                     blockingMeetingId)) {
                    possible = 0;
                    break;
                }
            }

            // Un participant peut être occupé par une mineure dans l'autre salle.
            // Ajouter aussi cette réunion à la liste des mineures à replacer.
            for(int p=0 ; p<meeting->nbParticipants && possible ; p++) {
                Member* member = getMemberById(pSharedData, meeting->participantIds[p]);
                if(member == NULL) {
                    possible = 0;
                    break;
                }

                for(int h=start ; h<start+meeting->duration ; h++) {
                    int blockingMeetingId = member->disponibilites[h];
                    if(blockingMeetingId == FREE_SLOT ||
                       blockingMeetingId == meeting->meetingId)
                        continue;

                    if(blockingMeetingId < 0 ||
                       blockingMeetingId >= MAX_AMOUNT_MEETING) {
                        possible = 0;
                        break;
                    }

                    Meeting* blockingMeeting =
                        &pSharedData->meetings[blockingMeetingId];
                    if(blockingMeeting->priorityOrder == MAJOR) {
                        possible = 0;
                        break;
                    }

                    if(!addMeetingToMove(meetingsIdToMove, &nbMeetingsToMove,
                                         blockingMeetingId)) {
                        possible = 0;
                        break;
                    }
                }
            }

            if(!possible) continue;

            Meeting meetingsToMove[MAX_AMOUNT_MEETING];

            // Conserver les anciennes informations, puis libérer les créneaux
            // des réunions mineures avant de réserver la majeure.
            for(int m=0 ; m<nbMeetingsToMove ; m++) {
                meetingsToMove[m] = pSharedData->meetings[meetingsIdToMove[m]];
                unBookMeeting(pSharedData, meetingsIdToMove[m]);
                pSharedData->meetings[meetingsIdToMove[m]].roomId = NO_ROOM;
            }

            // Réserver la salle et les agendas des participants pour la majeure.
            meeting->start = start;
            meeting->roomId = room->roomId;

            for(int h=start ; h<start+meeting->duration ; h++) {
                room->disponibilites[h] = meeting->meetingId;
            }

            for(int p=0 ; p<meeting->nbParticipants ; p++) {
                Member* member = getMemberById(pSharedData, meeting->participantIds[p]);

                if(member == NULL) continue;

                for(int h=start ; h<start+meeting->duration ; h++) {
                    member->disponibilites[h] = meeting->meetingId;
                }
            }

            // Rechercher un nouveau créneau pour chaque mineure déplacée.
            // Notifier les participants et journaliser le déplacement ou l'annulation.
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
    // Boucle principale du gestionnaire
    Message msg;
    while(1) {
        // Récupérer le message du membre (bloquant)
        if(msgrcv(msqid, &msg, MSG_SIZE, MANAGER_REQUEST_TYPE, 0) == -1) {
            perror("msgrcv");
            continue;
        }

        // Le stop ne consulte ni ne modifie les donnees partagees.
        if(msg.requestType == STOP_MANAGER)
            break;

        if(msg.requestType != DELETE_MEETING &&
           msg.requestType != ASK_REUNION)
            continue;

        if(lockSharedData(sharedData) == -1)
            continue;

        // Si le message est une demande de suppression de réunion
        if(msg.requestType == DELETE_MEETING) {
            int meetingId = msg.meeting.meetingId;
            // Vérifier que la réunion existe et qu'elle est bien bookée dans une salle
            if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING ||
               sharedData->meetings[meetingId].roomId == NO_ROOM) {
                addLog(sharedData, "Suppression impossible : reunion introuvable");
                unlockSharedData(sharedData);
                continue;
            }

            // Supprimer la réunion et notifier les participants
            Meeting* meeting = &sharedData->meetings[meetingId];
            unBookMeeting(sharedData, meetingId);
            notifyMeetingCancelled(msqid, sharedData, meetingId);
            meeting->roomId = NO_ROOM;
            if(sharedData->nbMeetings > 0)
                sharedData->nbMeetings--;

            char logMessage[128];
            snprintf(logMessage, sizeof(logMessage), "Reunion %d supprimee", meetingId);
            addLog(sharedData, logMessage);
            unlockSharedData(sharedData);
            continue;
        }

        // Si donc c'est une demande de réunion, on essaye de la booker
        // Vérifier les paramètres de la réunion demandée
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
            unlockSharedData(sharedData);
            continue;
        }

        // Vérifier qu'on a pas atteint le nombre max de réunions
        if(sharedData->nextMeetingId < 0 ||
           sharedData->nextMeetingId >= MAX_AMOUNT_MEETING) {
            notifyParticipants(msqid, &msg.meeting, NOTIFY_BOOKING_FAILED);
            addLog(sharedData, "Demande de reunion refusee (limite atteinte)");
            unlockSharedData(sharedData);
            continue;
        }

        // On assigne un ID à la réunion et on l'ajoute dans la mémoire partagée
        msg.meeting.meetingId = sharedData->nextMeetingId++;
        msg.meeting.roomId = NO_ROOM;
        sharedData->meetings[msg.meeting.meetingId] = msg.meeting;

        // On book la réunion selon sa priorité
        int booked;
        if(msg.meeting.priorityOrder == MINOR)
            booked = tryBookMinorMeeting(sharedData, msg.meeting.meetingId);
        else
            booked = tryBookMajorMeeting(msqid, sharedData, msg.meeting.meetingId);

        // Notifier les participants du résultat et ajouter un log
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
        unlockSharedData(sharedData);
    }
}