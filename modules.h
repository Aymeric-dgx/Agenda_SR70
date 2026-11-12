// modules.h

#ifndef MODULES_H
#define MODULES_H

#include "commun.h"

int initializeSharedSemaphore(SharedData* sharedData);
int lockSharedData(const SharedData* sharedData);
int unlockSharedData(const SharedData* sharedData);

/* Gestionnaire */

void gestionnaire(int msqid, SharedData* sharedData);

Member* getMemberById(SharedData* s, int idMember);

void unBookMeeting(SharedData* pSharedData, int idMeeting);

int tryBookMinorMeeting(SharedData* pSharedData, int idMeeting);

int tryBookMajorMeeting(int msqid, SharedData* pSharedData, int idMeeting);

void notifyMeetingAccepted(int msqid, SharedData* sharedData, int idMeeting);

void notifyMeetingRejected(int msqid, SharedData* sharedData, int idMeeting);

void notifyMeetingMoved(int msqid, SharedData* sharedData, int idMeeting);

void notifyMeetingCancelled(int msqid, SharedData* sharedData, int idMeeting);




/* Members */

void member(int msqid, SharedData* sharedData, int memberId);

void checkNotifications(int msqid, int memberId);

int requestMeeting(
    int msqid,
    int memberId,
    Priority priorityOrder,
    int start,
    int duration,
    int nbParticipants,
    int participantsId[]
);



/* UI */

void ui(int msqid, SharedData* sharedData);

#endif