#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "commun.h"
#include "gestionnaire.c"
#include "member.c"


int main() {
    // Création file de messages
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if(msqid == -1) {perror("msgget"); exit(EXIT_FAILURE);}


    // Création mémoire partagée
    int shmid = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if(shmid == -1) {perror("shmget"); exit(EXIT_FAILURE);}

    // Attachement 
    SharedData *shared = shmat(shmid, NULL, 0);
    if(shared == (void *)-1) {perror("shmat"); exit(EXIT_FAILURE);}


    // Initialisation
    shared->nbMeetings = 0;
    shared->nextMeetingId = 0;

    shared->rooms[0].roomId = ROOM_A;
    shared->rooms[0].capacity = SIZE_ROOM_A;
    shared->rooms[0].name = 'A';

    shared->rooms[1].roomId = ROOM_B;
    shared->rooms[1].capacity = SIZE_ROOM_B;
    shared->rooms[1].name = 'B';

    for(int i=0 ; i<NB_ROOM ; i++) {
        for(int j=0 ; j<NB_HOURES_IN_A_DAY ; j++) {
            shared->rooms[i].disponibilites[j] = -1;
        }
    }


    // Lancement parrallèle du gestionnaire
    pid_t gestionnairePid = fork();
    if(gestionnairePid == 0) {
        SharedData *shared = shmat(shmid, NULL, 0);
        gestionnaire(msqid, shared);
        exit(EXIT_SUCCESS);
    }


   
   /*----------------------------------Création des équipes et attribution des IDs uniques------------------------------------------*/

   // Création des équipes...
   int nextMemberId = 1;
    for(int team=0 ; team<NB_EQUIPES ; team++) {
        shared->teams[team].teamId = team + 1;

        int nbMembers = MIN_NB_MEMBERS + rand() % (MAX_NB_MEMBRES - MIN_NB_MEMBERS + 1);

        // Création + affectation à l'équipe des nouveaux membres (= processus)
        for(int member=0 ; member<nbMembers ; member++) {
            shared->teams[team].members[member].memberId = nextMemberId++;
            shared->teams[team].members[member].teamId = team + 1;

            // Initalisation de son agenda à FREE toute la journée
            for(int h=0 ; h<NB_HOURES_IN_A_DAY ; h++) {
                shared->teams[team].members[member].disponibilites[h] = FREE_SLOT;
            }

            // Lancement du processus / membre !
            pid_t pid = fork();
            if(pid == 0) {
                member(msqid, shared, shared->teams[team].members[member].memberId);
                exit(EXIT_SUCCESS);
            }

            shared->teams[team].members[member].pid = pid;
        }
    }


    /* ------------------------ */
    /* Nettoyage (attendre les wait pour tout les processus créé avant, donc gestionnaire + nbMembre) */
    while(wait(NULL) > 0);
    /* ------------------------ */

    shmdt(shared);
    msgctl(msqid, IPC_RMID, NULL);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}