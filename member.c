#include "commun.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/msg.h>


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
    srand(getpid());

    // Variable aléatoire pour demander un meeting

    while(1) {
        // Check non bloquant des potentielles notifications venant du gestionnaire
        checkNotifications(msqid, memberId);

        if(rand()%100 < 10) {
            requestMeeting(msqid, sharedData, memberId);
        }

        sleep(2);
    }
}





// Check-up (non bloquant) des notifications envoyé par le gestionnaire
void checkNotifications(int msqid, int memberId) {
    Message msg;

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
        }
    }
}





// Construction du meeting demandé + envoie au gestionnaire de la demande de booking de ce dernier
void requestMeeting(int msqid, SharedData* sharedData,int memberId, Priority priorityOrder, int start, int duration, int nbParticipants, int participantsId[]) {
    Meeting meeting;
    meeting.meetingId = sharedData->nextMeetingId++;
    meeting.priorityOrder = priorityOrder;
    meeting.duration = duration;
    meeting.start = start;
    meeting.nbParticipants =nbParticipants;
    for(int i=0 ; i<nbParticipants ; i++) {
        meeting.participantIds[i] = participantsId[i];
    }
    meeting.roomId = NO_ROOM; // Pas encore de salle assigné

    Message msg;
    msg.type = ASK_REUNION;
    msg.meeting = meeting;

    msgsnd(msqid, &msg, MSG_SIZE, 0);

    printf("[MEMBER %d] Demande réunion %d\n", memberId, meeting.meetingId);
}