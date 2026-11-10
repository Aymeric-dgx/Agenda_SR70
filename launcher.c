#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

#include "commun.h"
#include "modules.h"


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

    memset(shared, 0, sizeof(*shared));

    // Initialisation
    shared->nbMeetings = 4;
    shared->nextMeetingId = 72;
    shared->nbLogs = 0;

    shared->rooms[0].roomId = ROOM_A_ID;
    shared->rooms[0].capacity = SIZE_ROOM_A;
    strcpy(shared->rooms[0].name, "Salle A");
    

    shared->rooms[1].roomId = ROOM_B_ID;
    shared->rooms[1].capacity = SIZE_ROOM_B;
    strcpy(shared->rooms[1].name, "Salle B");

    for(int i=0 ; i<NB_ROOM ; i++) {
        for(int j=0 ; j<NB_HOURES_IN_A_DAY ; j++) {
            shared->rooms[i].disponibilites[j] = -1;
        }
    }

   

   /*----------------------------------Création des équipes et attribution des IDs uniques------------------------------------------*/
   // Création des équipes...
   int nextMemberId = 1;
    for(int team=0 ; team<NB_EQUIPES ; team++) {
        shared->teams[team].teamId = team + 1;

        int nbMembers = MIN_NB_MEMBERS + rand() % (MAX_NB_MEMBRES - MIN_NB_MEMBERS + 1);

        // Création + affectation à l'équipe des nouveaux membres (= processus)
        for(int Member=0 ; Member<nbMembers ; Member++) {
            shared->teams[team].members[Member].memberId = nextMemberId++;
            shared->teams[team].members[Member].teamId = team + 1;

            // Initalisation de son agenda à FREE toute la journée
            for(int h=0 ; h<NB_HOURES_IN_A_DAY ; h++) {
                shared->teams[team].members[Member].disponibilites[h] = FREE_SLOT;
            }

            // Lancement du processus / membre
            pid_t pid = fork();
            if(pid == 0) {
                //member(msqid, shared, shared->teams[team].members[Member].memberId);
                exit(EXIT_SUCCESS);
            }

            shared->teams[team].members[Member].pid = pid;
        }
    }



    // Lancement du gestionnaire après l'initialisation des équipes.
    pid_t gestionnairePid = fork();
    if(gestionnairePid == 0) {
        gestionnaire(msqid, shared);
        exit(EXIT_SUCCESS);
    }

    // Lancement de l'UI une fois les données partagées initialisées.
    pid_t uiPid = fork();
    if(uiPid == 0) {
        ui(msqid, shared);
        exit(EXIT_SUCCESS);
    }

    // Attente de la fin de l'UI avant de terminer le programme
    waitpid(uiPid, NULL, 0);

    // Shutdown les membres
    for(int team = 0; team < NB_EQUIPES; team++) {
        for(int member = 0; member < MAX_NB_MEMBRES; member++) {
            pid_t memberPid = shared->teams[team].members[member].pid;
            if(memberPid > 0 && waitpid(memberPid, NULL, WNOHANG) == 0 &&
               kill(memberPid, SIGTERM) == -1 && errno != ESRCH)
                perror("kill");
        }
    }

    // Envoyer un message de shutdown au gestionnaire
    Message shutdownMessage = {0};
    shutdownMessage.type = MANAGER_REQUEST_TYPE;
    shutdownMessage.requestType = STOP_MANAGER;
    if(msgsnd(msqid, &shutdownMessage, MSG_SIZE, IPC_NOWAIT) == -1) {
        perror("msgsnd");
        kill(gestionnairePid, SIGTERM);
    }
    waitpid(gestionnairePid, NULL, 0);


    // Nettoyage (attendre les wait pour tout les processus créé avant, donc gestionnaire + nbMembre)
    while(wait(NULL) > 0);


    shmdt(shared);
    msgctl(msqid, IPC_RMID, NULL);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}