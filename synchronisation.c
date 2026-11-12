#include "modules.h"
#include <errno.h>
#include <stdio.h>
#include <sys/sem.h>

#ifdef _SEM_SEMUN_UNDEFINED
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};
#endif


// Initialiser le sémaphore pour la synchronisation des accès à la mémoire partagée
int initializeSharedSemaphore(SharedData* sharedData) {
    // Créer un sémaphore pour la synchronisation des accès à la mémoire partagée
    sharedData->semaphoreId = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if(sharedData->semaphoreId == -1) {
        perror("semget");
        return -1;
    }

    // Initialiser le sémaphore à 1 (libre)
    union semun value;
    value.val = 1;

    // Vérifier si l'initialisation du sémaphore a réussi
    if(semctl(sharedData->semaphoreId, 0, SETVAL, value) == -1) {
        perror("semctl(SETVAL)");
        semctl(sharedData->semaphoreId, 0, IPC_RMID);
        return -1;
    }

    return 0;
}


// Verrouiller l'accès à la mémoire partagée
int lockSharedData(const SharedData* sharedData) {
    // Définir l'opération de verrouillage du sémaphore
    struct sembuf operation = {0, -1, SEM_UNDO};

    // Effectuer l'opération de verrouillage du sémaphore
    while(semop(sharedData->semaphoreId, &operation, 1) == -1) {
        if(errno == EINTR)
            continue;
        perror("semop(lock)");
        return -1;
    }

    return 0;
}


// Déverrouiller l'accès à la mémoire partagée
int unlockSharedData(const SharedData* sharedData) {
    // Définir l'opération de déverrouillage du sémaphore
    struct sembuf operation = {0, 1, SEM_UNDO};

    // Effectuer l'opération de déverrouillage du sémaphore
    while(semop(sharedData->semaphoreId, &operation, 1) == -1) {
        if(errno == EINTR)
            continue;
        perror("semop(unlock)");
        return -1;
    }

    return 0;
}
