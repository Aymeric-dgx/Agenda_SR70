#include "commun.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/msg.h>



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
    msg.requestType = ASK_REUNION;

    msgsnd(msqid, &msg, MSG_SIZE, 0);

    printf("[MEMBER %d] Demande réunion %d\n", memberId, meeting.meetingId);
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
    srand(getpid());

    while(1) {
        // Variable aléatoire pour demander un meeting
        Priority priorityOrder = (rand()%5 == 0) ? MAJOR : MINOR;
        int duration = 1 + rand()%3; // 1h à 3h
        int start = (rand()%2) ? UNSPECIFIED_START : rand()%(NB_HOURES_IN_A_DAY-duration+1); // 50% du temps : pas d'heure imposée
        int nbParticipants = 1 + rand()%5; // Entre 1 et 5 participants
        int participantsId[SIZE_BIGGEST_ROOM]; 
        participantsId[0] = memberId; // Le créateur participe toujours à sa réunion
        // Participants aléatoires
        for(int i=1 ; i<nbParticipants ; i++) {
            int valid = 0;
            while(!valid) {
                int candidate = 1 + rand()%60;
                valid = 1;

                for(int j=0 ; j<i ; j++) {
                    if(participantsId[j] == candidate) {
                        valid = 0;
                        break;
                    }
                }

                if(valid) participantsId[i] = candidate;
            }
        }



        // Check non bloquant des potentielles notifications venant du gestionnaire
        checkNotifications(msqid, memberId);

        if(rand()%100 < 10) {
            requestMeeting(msqid, sharedData, memberId, priorityOrder, start, duration, nbParticipants, participantsId);
        }

        sleep(2);
    }
}