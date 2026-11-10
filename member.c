#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/msg.h>



// Check-up (non bloquant) des notifications envoyé par le gestionnaire
void checkNotifications(int msqid, int memberId) {
    Message msg;
    // Tant qu'il y a des messages dans la file, les traiter    
    while(msgrcv(msqid, &msg, MSG_SIZE, memberId, IPC_NOWAIT) != -1) {
        switch(msg.requestType) {

            case NOTIFY_BOOKING_SUCCESS:
                printf("[MEMBER %d] Réunion %d acceptée\n", memberId, msg.meeting.meetingId);
                break;

            case NOTIFY_BOOKING_FAILED:
                printf("[MEMBER %d] Réunion %d refusée\n", memberId, msg.meeting.meetingId);
                break;

            case NOTIFY_MEETING_MOVED:
                printf("[MEMBER %d] Réunion %d déplacée\n", memberId, msg.meeting.meetingId);
                break;

            case NOTIFY_MEETING_CANCELLED:
                printf("[MEMBER %d] Réunion %d annulée\n", memberId, msg.meeting.meetingId);
                break;

            default:
                break;
        }
    }
}




// NB : Anciennement utilisé dans le member, maintenant utilisé dans l'UI pour envoyer une demande de réunion au gestionnaire
// Construction du meeting demandé + envoie au gestionnaire de la demande de booking de ce dernier
int requestMeeting(int msqid, int memberId, Priority priorityOrder, int start, int duration, int nbParticipants, int participantsId[]) {
    // Vérification des paramètres de la réunion
    if(nbParticipants < 1 || nbParticipants > SIZE_BIGGEST_ROOM ||
       duration < 1 || duration > NB_HOURES_IN_A_DAY ||
       (start != UNSPECIFIED_START &&
        (start < 0 || start > NB_HOURES_IN_A_DAY - duration))) {
        errno = EINVAL;
        return -1;
    }

    // Construction du meeting
    Meeting meeting = {0};
    meeting.meetingId = UNSPECIFIED_START;
    meeting.priorityOrder = priorityOrder;
    meeting.duration = duration;
    meeting.start = start;
    meeting.nbParticipants =nbParticipants;
    for(int i=0 ; i<nbParticipants ; i++) {
        meeting.participantIds[i] = participantsId[i];
    }
    meeting.roomId = NO_ROOM; // Pas encore de salle assigné

    Message msg = {0};
    msg.type = MANAGER_REQUEST_TYPE;
    msg.meeting = meeting;
    msg.requestType = ASK_REUNION;

    // Envoie de la demande au gestionnaire
    if(msgsnd(msqid, &msg, MSG_SIZE, IPC_NOWAIT) == -1) {
        perror("msgsnd");
        return -1;
    }

    // Affichage d'un message pour le membre
    printf("[MEMBER %d] Demande de réunion envoyée\n", memberId);
    return 0;
}






/*
Doit pouvoir :
    --> Créer des demandes de réunion
    - ->Recevoir des notifications
        --> réunion déplacée
        --> réunion annulée
        --> réunion acceptée
    --> Continuer à "vivre" pendant toute la simulation.    
*/
void member(int msqid, SharedData* sharedData, int memberId) {
    (void)sharedData;

    while(1) {
        // Check non bloquant des potentielles notifications venant du gestionnaire
        checkNotifications(msqid, memberId);
        sleep(2);
    }
}