#ifndef COMMUN_H
#define COMMUN_H

#define NB_EQUIPES 6

#define MIN_NB_MEMBERS 5
#define MAX_NB_MEMBRES 10

#define NB_ROOM 2
#define SIZE_ROOM_A 8
#define SIZE_ROOM_B 15
#define SIZE_BIGGEST_ROOM 15

#define NB_HOURES_IN_A_DAY 10

#define FREE_SLOT -1 // Pour dire que la salle/membre est dispo, on mets -1 dans ses disponilité !

#define MANAGER_REQUEST_TYPE 1000L
#define MSG_SIZE (sizeof(Message) - sizeof(long))

#define MAX_AMOUNT_MEETING 100

#define UNSPECIFIED_START -1

#define NO_ROOM -1

#define MAX_LOGS 200


typedef enum {
    MINOR,
    MAJOR
} Priority;

typedef enum {
    ASK_REUNION = 1,
    DELETE_MEETING,
    STOP_MANAGER,

    NOTIFY_BOOKING_SUCCESS,
    NOTIFY_BOOKING_FAILED,

    NOTIFY_MEETING_MOVED,
    NOTIFY_MEETING_CANCELLED
} MsgRequestType;


typedef enum {
    ROOM_A_ID = 1,
    ROOM_B_ID = 2
} roomName;



// Pour stocker les messages pour l'historique UI
typedef struct {
    char message[128];
} LogEntry;



// Salle
typedef struct {
    int roomId;
    char name[16];
    int capacity;
    int disponibilites[NB_HOURES_IN_A_DAY]; // Ex : [1, 1, -1, -1, 2, 0, ...] ----> 8H-10H = Réunion n°1, 10H-12H : dispo, 12H-13H : Réunion n°2
} Room;



// Membre de l'équipe
typedef struct {
    int memberId;
    int teamId;
    int pid;
    int disponibilites[NB_HOURES_IN_A_DAY];
} Member;



//Reunion
typedef struct {
    int meetingId;
    Priority priorityOrder;
    int participantIds[SIZE_BIGGEST_ROOM];
    int nbParticipants;
    int duration;
    int start;
    int roomId;
} Meeting;




//Messages IPC
typedef struct {
    long type; // destinataire dans le cas d'un envoi gestionnaire --> member
    MsgRequestType requestType; // Sert à spécifier au member quel type de notifications c'est (succès/echec booking, ou bien déplacement/suppression réunion)
    Meeting meeting;

} Message;



//Equipe
typedef struct {
    int teamId;
    Member members[MAX_NB_MEMBRES];
} Team;



// Structure des données mise en mémoire commune
typedef struct {
    Room rooms[NB_ROOM];
    Team teams[NB_EQUIPES];
    Meeting meetings[MAX_AMOUNT_MEETING];
    int nbMeetings;
    int nextMeetingId;
    LogEntry logs[MAX_LOGS];
    int nbLogs;
} SharedData;

#endif