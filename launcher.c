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

    /*----------------------------------Meetings de test------------------------------------------*/

    // Meeting 12 (MAJOR) Salle A 9H-11H

    shared->meetings[12].meetingId = 12;
    shared->meetings[12].priorityOrder = MAJOR;
    shared->meetings[12].duration = 2;
    shared->meetings[12].start = 1;
    shared->meetings[12].roomId = ROOM_A_ID;
    shared->meetings[12].nbParticipants = 4;
    shared->meetings[12].participantIds[0] = 1;
    shared->meetings[12].participantIds[1] = 2;
    shared->meetings[12].participantIds[2] = 3;
    shared->meetings[12].participantIds[3] = 4;

    shared->rooms[0].disponibilites[1] = 12;
    shared->rooms[0].disponibilites[2] = 12;

    // Meeting 23 (MINOR) Salle A 12H-14H

    shared->meetings[23].meetingId = 23;
    shared->meetings[23].priorityOrder = MINOR;
    shared->meetings[23].duration = 2;
    shared->meetings[23].start = 4;
    shared->meetings[23].roomId = ROOM_A_ID;
    shared->meetings[23].nbParticipants = 2;
    shared->meetings[23].participantIds[0] = 1;
    shared->meetings[23].participantIds[1] = 2;

    shared->rooms[0].disponibilites[4] = 23;
    shared->rooms[0].disponibilites[5] = 23;

    // Meeting 45 (MAJOR) Salle B 8H-10H

    shared->meetings[45].meetingId = 45;
    shared->meetings[45].priorityOrder = MAJOR;
    shared->meetings[45].duration = 2;
    shared->meetings[45].start = 0;
    shared->meetings[45].roomId = ROOM_B_ID;
    shared->meetings[45].nbParticipants = 5;
    shared->meetings[45].participantIds[0] = 5;
    shared->meetings[45].participantIds[1] = 6;
    shared->meetings[45].participantIds[2] = 7;
    shared->meetings[45].participantIds[3] = 8;
    shared->meetings[45].participantIds[4] = 9;

    shared->rooms[1].disponibilites[0] = 45;
    shared->rooms[1].disponibilites[1] = 45;

    // Meeting 71 (MINOR) Salle B 14H-17H

    shared->meetings[71].meetingId = 71;
    shared->meetings[71].priorityOrder = MINOR;
    shared->meetings[71].duration = 3;
    shared->meetings[71].start = 6;
    shared->meetings[71].roomId = ROOM_B_ID;
    shared->meetings[71].nbParticipants = 3;
    shared->meetings[71].participantIds[0] = 8;
    shared->meetings[71].participantIds[1] = 9;
    shared->meetings[71].participantIds[2] = 10;

    shared->rooms[1].disponibilites[6] = 71;
    shared->rooms[1].disponibilites[7] = 71;
    shared->rooms[1].disponibilites[8] = 71;

    /*----------------------------------Fin meetings de test------------------------------------------*/


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

            // Lancement du processus / membre !
            pid_t pid = fork();
            if(pid == 0) {
                //member(msqid, shared, shared->teams[team].members[Member].memberId);
                exit(EXIT_SUCCESS);
            }

            shared->teams[team].members[Member].pid = pid;
        }
    }

    int demoMeetingIds[] = {12, 23, 45, 71};
    for(size_t meetingIndex = 0;
        meetingIndex < sizeof(demoMeetingIds) / sizeof(demoMeetingIds[0]);
        meetingIndex++) {
        Meeting* meeting = &shared->meetings[demoMeetingIds[meetingIndex]];
        for(int participant = 0; participant < meeting->nbParticipants; participant++) {
            Member* member = getMemberById(shared, meeting->participantIds[participant]);
            if(member == NULL)
                continue;

            for(int hour = meeting->start;
                hour < meeting->start + meeting->duration;
                hour++)
                member->disponibilites[hour] = meeting->meetingId;
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

    waitpid(uiPid, NULL, 0);

    for(int team = 0; team < NB_EQUIPES; team++) {
        for(int member = 0; member < MAX_NB_MEMBRES; member++) {
            pid_t memberPid = shared->teams[team].members[member].pid;
            if(memberPid > 0 && waitpid(memberPid, NULL, WNOHANG) == 0 &&
               kill(memberPid, SIGTERM) == -1 && errno != ESRCH)
                perror("kill");
        }
    }

    Message shutdownMessage = {0};
    shutdownMessage.type = MANAGER_REQUEST_TYPE;
    shutdownMessage.requestType = STOP_MANAGER;
    if(msgsnd(msqid, &shutdownMessage, MSG_SIZE, IPC_NOWAIT) == -1) {
        perror("msgsnd");
        kill(gestionnairePid, SIGTERM);
    }
    waitpid(gestionnairePid, NULL, 0);

    /* ------------------------ */
    /* Nettoyage (attendre les wait pour tout les processus créé avant, donc gestionnaire + nbMembre) */
    while(wait(NULL) > 0);
    /* ------------------------ */

    shmdt(shared);
    msgctl(msqid, IPC_RMID, NULL);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}