#include "commun.h"

#include <stdlib.h>



void gestionnaire(int msqid, SharedData* sharedData) {
    Message msg;
    while(1) {
        msgrcv(msqid, &msg, MSG_SIZE, ASK_REUNION, 0);
        sharedData->meetings[msg.meeting.meetingId] = msg.meeting;

        if(msg.meeting.priorityOrder == MINOR) {
            if(tryBookMinorMeeting(sharedData, msg.meeting.meetingId)) {
                // Prévenir les participants que le booking a réussi
                notifyMeetingAccepted(msqid, sharedData, msg.meeting.meetingId);
            }
            else {
                // Prévenir les participants que le booking a échoué
                notifyMeetingRejected(msqid, sharedData, msg.meeting.meetingId);
            }
        }
        else {
            if(tryBookMajorMeeting(sharedData, msg.meeting.meetingId)) {
                // Prévenir les participants que le booking a réussi
                notifyMeetingAccepted(msqid, sharedData, msg.meeting.meetingId);
            }
            else {
                // Prévenir les participants que le booking a échoué
                notifyMeetingRejected(msqid, sharedData, msg.meeting.meetingId);
            }
        }
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
int tryBookMajorMeeting(SharedData* pSharedData, int idMeeting) {
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

            // Si il n'y a pas eu de souci de major, unbooker toutes les minors néscessiares
            for(int m=0 ; m<nbMeetingsToMove ; m++) {
                unBookMeeting(pSharedData, meetingsIdToMove[m]);
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

            // Enfin, essayer de rebooker les minors qui ont été supprimé (potentiel annulation total, donc envoyer message au particpants pour les prévenir !)
            for(int m=0 ; m<nbMeetingsToMove ; m++) {

                if(!tryBookMinorMeeting(pSharedData, meetingsIdToMove[m])) {
                    /*
                     * TODO :
                     * notifier annulation
                     */
                }
                else {
                    /*
                     * TODO :
                     * notifier déplacement
                     */
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

    for(int i=0 ; i<meeting->nbParticipants ; i++) {
        Message msg;

        msg.type = meeting->participantIds[i];
        msg.requestType = NOTIFY_MEETING_CANCELLED; // Permet au membre de savoir que la réunion a été supprimée
        msg.meeting = *meeting;

        msgsnd(msqid, &msg, MSG_SIZE, 0);
    }
}



// Notifier les particpants d'une réunion qu'elle a été déplacé (suite à une replanification à cause d'une major)
void notifyMeetingMoved(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];

    for(int i=0 ; i<meeting->nbParticipants ; i++) {

        Message msg;

        msg.type = meeting->participantIds[i];
        msg.requestType = NOTIFY_MEETING_MOVED; // Permet au membre de savoir que sa réunion a été déplacée
        msg.meeting = *meeting;

        msgsnd(msqid, &msg, MSG_SIZE, 0);
    }
}




// Notifier les particpants d'une réunion qu'elle a été déplacé (suite à une demande de booking de réunion d'un membre)
void notifyMeetingAccepted(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];

    for(int i=0 ; i<meeting->nbParticipants ; i++) {
        Message msg;

        msg.type = meeting->participantIds[i];
        msg.requestType = NOTIFY_BOOKING_SUCCESS; // Permet au membre de savoir que le booking a réussi
        msg.meeting = *meeting;

        msgsnd(msqid, &msg, MSG_SIZE, 0);
    }
}



// Notifier les participants qu'il n'a pas été possible de booker la réunion
void notifyMeetingRejected(int msqid, SharedData* sharedData, int idMeeting) {
    Meeting* meeting = &sharedData->meetings[idMeeting];

    for(int i=0 ; i<meeting->nbParticipants ; i++) {
        Message msg;

        msg.type = meeting->participantIds[i];
        msg.requestType = NOTIFY_BOOKING_FAILED;
        msg.meeting = *meeting;

        msgsnd(msqid, &msg, MSG_SIZE, 0);
    }
}