#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>

#include "modules.h"

typedef enum {
    PAGE_HOME,
    PAGE_MEMBER,
    PAGE_HISTORY,
    PAGE_TEAMS
} Page;

// Regroupe l'etat d'affichage et les choix en cours dans les formulaires.
typedef struct {
    Page page;
    int selectedRoom;
    int selectedHour;
    int selectedMemberId;
    int selectedTeam;
    int participantTeam;
    Priority priority;
    int duration;
    int start;
    int participantIds[SIZE_BIGGEST_ROOM];
    int participantCount;
    int historyScroll;
    char status[128];
} UIState;

// Convertit un texte en texture SDL, l'affiche, puis libere les ressources temporaires.
static void drawText(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y) {
    if(font == NULL || text == NULL)
        return;

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, white);
    if(surface == NULL) {
        fprintf(stderr, "Impossible de dessiner le texte : %s\n", TTF_GetError());
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if(texture == NULL) {
        fprintf(stderr, "Impossible de creer la texture du texte : %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect destination = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

// Les fonctions get*Rect calculent les zones d'affichage et de clic
// a partir de la taille courante de la fenetre.
static SDL_Rect getHomeButtonRect(int width, int height) {
    SDL_Rect rect = {width / 50, height / 50, width / 8, height / 14};
    return rect;
}

static SDL_Rect getTeamsButtonRect(int width, int height) {
    SDL_Rect rect = {width * 14 / 100, height / 50, width / 8, height / 14};
    return rect;
}

static SDL_Rect getHistoryButtonRect(int width, int height) {
    SDL_Rect rect = {width * 27 / 100, height / 50, width / 6, height / 14};
    return rect;
}

static int getRoomTop(int roomIndex, int height) {
    return roomIndex == 0 ? height / 8 : height * 40 / 100;
}

static SDL_Rect getSlotRect(int roomIndex, int hour, int width, int height) {
    int startX = width / 20;
    int slotStep = width * 90 / 100 / NB_HOURES_IN_A_DAY;
    int slotGap = slotStep / 10;

    SDL_Rect slot = {
        startX + hour * slotStep,
        getRoomTop(roomIndex, height) + height / 12,
        slotStep - slotGap,
        height / 12
    };
    return slot;
}

static SDL_Rect getDetailsBox(int width, int height) {
    SDL_Rect rect = {width / 30, height * 70 / 100, width * 93 / 100, height * 25 / 100};
    return rect;
}

static SDL_Rect getDeleteButtonRect(int width, int height) {
    SDL_Rect details = getDetailsBox(width, height);
    int lineSpacing = details.h / 6;
    SDL_Rect rect = {
        details.x + details.w * 73 / 100,
        details.y + lineSpacing,
        details.w / 5,
        details.h / 4
    };
    return rect;
}

// Dessine un bouton commun aux differentes pages.
static void drawButton(SDL_Renderer* renderer, TTF_Font* font, SDL_Rect rect,
                       const char* label, int selected) {
    if(selected)
        SDL_SetRenderDrawColor(renderer, 45, 105, 75, 255);
    else
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);

    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
    SDL_RenderDrawRect(renderer, &rect);
    drawText(renderer, font, label, rect.x + rect.w / 10, rect.y + rect.h / 4);
}

// Affiche les boutons de navigation et indique la page active.
static void drawNavigation(SDL_Renderer* renderer, TTF_Font* font, Page page,
                           int width, int height) {
    SDL_Rect home = getHomeButtonRect(width, height);
    SDL_Rect teams = getTeamsButtonRect(width, height);
    SDL_Rect history = getHistoryButtonRect(width, height);

    drawButton(renderer, font, home, "Accueil", page == PAGE_HOME);
    drawButton(renderer, font, teams, "Equipes", page == PAGE_TEAMS || page == PAGE_MEMBER);
    drawButton(renderer, font, history, "Historique", page == PAGE_HISTORY);
}

// Affiche les heures et les reservations d'une salle.
// Les couleurs distinguent les reunions MAJOR des reunions MINOR.
static void drawRoomAgenda(SDL_Renderer* renderer, TTF_Font* font,
                           SharedData* sharedData, int roomIndex,
                           int width, int height) {
    int roomTop = getRoomTop(roomIndex, height);
    int startX = width / 20;
    int slotStep = width * 90 / 100 / NB_HOURES_IN_A_DAY;
    int slotHeight = height / 12;

    drawText(renderer, font,
             roomIndex == 0 ? "Salle A" : "Salle B",
             width / 40, roomTop);

    for(int hour = 0; hour < NB_HOURES_IN_A_DAY; hour++) {
        char hourText[10];
        snprintf(hourText, sizeof(hourText), "%d", hour + 8);
        drawText(renderer, font, hourText,
                 startX + hour * slotStep + slotStep / 3,
                 roomTop + height / 24);
    }

    for(int hour = 0; hour < NB_HOURES_IN_A_DAY; hour++) {
        SDL_Rect slot = getSlotRect(roomIndex, hour, width, height);
        int meetingId = sharedData->rooms[roomIndex].disponibilites[hour];

        if(meetingId == FREE_SLOT) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_RenderDrawRect(renderer, &slot);
            continue;
        }

        if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING)
            continue;

        Meeting* meeting = &sharedData->meetings[meetingId];
        if(meeting->priorityOrder == MAJOR)
            SDL_SetRenderDrawColor(renderer, 200, 60, 60, 255);
        else
            SDL_SetRenderDrawColor(renderer, 60, 120, 255, 255);

        SDL_RenderFillRect(renderer, &slot);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &slot);

        char label[16];
        snprintf(label, sizeof(label), "M%d", meetingId);
        drawText(renderer, font, label,
                 slot.x + slot.w / 6, slot.y + slotHeight / 4);
    }
}

// Affiche les informations du creneau choisi ou indique qu'il est libre.
static void drawSelectedSlot(SDL_Renderer* renderer, TTF_Font* font,
                             SharedData* sharedData, UIState* state,
                             int width, int height) {
    SDL_Rect details = getDetailsBox(width, height);
    int lineSpacing = details.h / 6;
    int textX = details.x + details.w / 50;

    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &details);
    drawText(renderer, font, "Details", textX, details.y + lineSpacing / 3);

    if(state->selectedRoom < 0 || state->selectedRoom >= NB_ROOM ||
       state->selectedHour < 0 || state->selectedHour >= NB_HOURES_IN_A_DAY) {
        drawText(renderer, font, "Aucun creneau selectionne",
                 textX, details.y + lineSpacing);
        return;
    }

    int meetingId =
        sharedData->rooms[state->selectedRoom].disponibilites[state->selectedHour];
    if(meetingId == FREE_SLOT) {
        char freeSlot[64];
        snprintf(freeSlot, sizeof(freeSlot), "Creneau libre - Salle %c, %dH",
                 state->selectedRoom == 0 ? 'A' : 'B',
                 state->selectedHour + 8);
        drawText(renderer, font, freeSlot, textX, details.y + lineSpacing);
        return;
    }

    if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING) {
        drawText(renderer, font, "Informations indisponibles",
                 textX, details.y + lineSpacing);
        return;
    }

    Meeting* meeting = &sharedData->meetings[meetingId];
    char text[128];

    snprintf(text, sizeof(text), "Reunion M%d", meetingId);
    drawText(renderer, font, text, textX, details.y + lineSpacing);

    snprintf(text, sizeof(text), "Priorite : %s",
             meeting->priorityOrder == MAJOR ? "MAJEURE" : "MINEURE");
    drawText(renderer, font, text, textX, details.y + lineSpacing * 2);

    snprintf(text, sizeof(text), "Debut : %dH", meeting->start + 8);
    drawText(renderer, font, text, textX, details.y + lineSpacing * 3);

    snprintf(text, sizeof(text), "Duree : %dH", meeting->duration);
    drawText(renderer, font, text, textX, details.y + lineSpacing * 4);

    snprintf(text, sizeof(text), "Participants : %d", meeting->nbParticipants);
    drawText(renderer, font, text, textX, details.y + lineSpacing * 5);

    drawButton(renderer, font, getDeleteButtonRect(width, height),
               "SUPPRIMER", 0);
}

// La page d'accueil rassemble les agendas des salles et les details selectionnes.
static void drawHomePage(SDL_Renderer* renderer, TTF_Font* font,
                         SharedData* sharedData, UIState* state,
                         int width, int height) {
    drawRoomAgenda(renderer, font, sharedData, 0, width, height);
    drawRoomAgenda(renderer, font, sharedData, 1, width, height);
    drawSelectedSlot(renderer, font, sharedData, state, width, height);
}

static SDL_Rect getTeamButtonRect(int teamIndex, int width, int height) {
    int rowHeight = height * 11 / 100;
    SDL_Rect rect = {
        width / 25,
        height * 16 / 100 + teamIndex * rowHeight,
        width * 30 / 100,
        rowHeight * 4 / 5
    };
    return rect;
}

static SDL_Rect getMemberButtonRect(int memberIndex, int width, int height) {
    int panelX = width * 39 / 100;
    int rowHeight = height * 7 / 100;
    SDL_Rect rect = {
        panelX + width / 100,
        height * 19 / 100 + memberIndex * rowHeight,
        width * 53 / 100,
        rowHeight * 9 / 10
    };
    return rect;
}

// Affiche la liste des equipes et, pour l'equipe selectionnee, ses membres.
static void drawTeamsPage(SDL_Renderer* renderer, TTF_Font* font,
                          SharedData* sharedData, UIState* state,
                          int width, int height) {
    drawText(renderer, font, "Equipes", width / 25, height * 11 / 100);

    SDL_Rect teamPanel = {
        width / 40, height * 15 / 100, width * 32 / 100, height * 80 / 100
    };
    SDL_Rect memberPanel = {
        width * 38 / 100, height * 15 / 100,
        width * 58 / 100, height * 80 / 100
    };
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &teamPanel);
    SDL_RenderDrawRect(renderer, &memberPanel);

    for(int teamIndex = 0; teamIndex < NB_EQUIPES; teamIndex++) {
        char label[32];
        snprintf(label, sizeof(label), "Equipe %d %s",
                 teamIndex + 1,
                 state->selectedTeam == teamIndex ? "v" : ">");
        drawButton(renderer, font,
                   getTeamButtonRect(teamIndex, width, height),
                   label, state->selectedTeam == teamIndex);
    }

    if(state->selectedTeam < 0 || state->selectedTeam >= NB_EQUIPES) {
        drawText(renderer, font, "Selectionnez une equipe",
                 memberPanel.x + memberPanel.w / 20,
                 memberPanel.y + memberPanel.h / 12);
        return;
    }

    Team* team = &sharedData->teams[state->selectedTeam];
    int displayedIndex = 0;
    for(int memberIndex = 0; memberIndex < MAX_NB_MEMBRES; memberIndex++) {
        Member* member = &team->members[memberIndex];
        if(member->memberId <= 0)
            continue;

        char label[32];
        snprintf(label, sizeof(label), "Membre %d", member->memberId);
        drawButton(renderer, font,
                   getMemberButtonRect(displayedIndex, width, height),
                   label, 0);
        displayedIndex++;
    }

    if(displayedIndex == 0)
        drawText(renderer, font, "Aucun membre dans cette equipe",
                 memberPanel.x + memberPanel.w / 20,
                 memberPanel.y + memberPanel.h / 12);
}

// Affiche les evenements les plus recents en premier.
// historyScroll est borne au nombre de lignes qui peuvent etre affichees.
static void drawHistoryPage(SDL_Renderer* renderer, TTF_Font* font,
                            SharedData* sharedData, UIState* state,
                            int width, int height) {
    SDL_Rect historyPanel = {
        width / 25, height * 16 / 100, width * 95 / 100, height * 80 / 100
    };
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &historyPanel);
    drawText(renderer, font, "Historique des reunions",
             historyPanel.x + historyPanel.w / 50,
             height * 10 / 100);

    int logCount = sharedData->nbLogs;
    if(logCount < 0)
        logCount = 0;
    if(logCount > MAX_LOGS)
        logCount = MAX_LOGS;

    int rowHeight = height / 18;
    int visibleRows = (historyPanel.h - rowHeight / 2) / rowHeight;
    int maxScroll = logCount > visibleRows ? logCount - visibleRows : 0;
    if(state->historyScroll > maxScroll)
        state->historyScroll = maxScroll;
    if(state->historyScroll < 0)
        state->historyScroll = 0;

    if(logCount == 0) {
        drawText(renderer, font, "Aucun evenement pour le moment",
                 historyPanel.x + historyPanel.w / 30,
                 historyPanel.y + rowHeight);
        return;
    }

    for(int row = 0; row < visibleRows; row++) {
        int logIndex = logCount - 1 - state->historyScroll - row;
        if(logIndex < 0)
            break;
        drawText(renderer, font, sharedData->logs[logIndex].message,
                 historyPanel.x + historyPanel.w / 30,
                 historyPanel.y + rowHeight / 2 + row * rowHeight);
    }
}

// Indique si un membre fait deja partie de la demande en cours.
static int hasParticipant(const UIState* state, int memberId) {
    for(int i = 0; i < state->participantCount; i++) {
        if(state->participantIds[i] == memberId)
            return 1;
    }
    return 0;
}

// Les fonctions suivantes calculent les zones des controles du formulaire
// de demande de reunion.
static SDL_Rect getParticipantButtonRect(int index, int width, int height) {
    int step = width * 9 / 100;
    int column = index % 5;
    int row = index / 5;
    SDL_Rect rect = {
        width / 2 + column * step,
        height * (72 + row * 6) / 100,
        step * 85 / 100,
        height / 18
    };
    return rect;
}

static SDL_Rect getParticipantTeamButtonRect(int option, int width, int height) {
    int positions[] = {20, 26, 40};
    int widths[] = {5, 13, 5};
    SDL_Rect rect = {
        width * positions[option] / 100,
        height * 72 / 100,
        width * widths[option] / 100,
        height / 18
    };
    return rect;
}

static SDL_Rect getPriorityButtonRect(int option, int width, int height) {
    int step = width * 16 / 100;
    SDL_Rect rect = {
        width / 5 + option * step,
        height * 49 / 100,
        step * 9 / 10,
        height / 16
    };
    return rect;
}

static SDL_Rect getDurationButtonRect(int option, int width, int height) {
    int step = width * 11 / 100;
    SDL_Rect rect = {
        width / 5 + option * step,
        height * 57 / 100,
        step * 9 / 10,
        height / 16
    };
    return rect;
}

static SDL_Rect getStartButtonRect(int option, int width, int height) {
    int positions[] = {20, 36, 46, 56};
    int buttonWidths[] = {14, 8, 9, 8};
    SDL_Rect rect = {
        width * positions[option] / 100,
        height * 65 / 100,
        width * buttonWidths[option] / 100,
        height / 16
    };
    return rect;
}

static SDL_Rect getSubmitButtonRect(int width, int height) {
    SDL_Rect rect = {
        width * 75 / 100, height * 86 / 100,
        width / 5, height / 12
    };
    return rect;
}

static SDL_Rect getBackButtonRect(int width, int height) {
    SDL_Rect rect = {
        width * 79 / 100, height * 10 / 100,
        width / 6, height / 14
    };
    return rect;
}

// Affiche l'agenda d'un membre, avec le meme code couleur que l'agenda des salles.
static void drawMemberAgenda(SDL_Renderer* renderer, TTF_Font* font,
                             SharedData* sharedData, Member* member,
                             int width, int height) {
    int startX = width / 20;
    int slotStep = width * 90 / 100 / NB_HOURES_IN_A_DAY;
    int slotHeight = height / 10;

    drawText(renderer, font, "Planning du membre",
             width / 25, height * 17 / 100);

    for(int hour = 0; hour < NB_HOURES_IN_A_DAY; hour++) {
        char hourText[10];
        snprintf(hourText, sizeof(hourText), "%d", hour + 8);
        drawText(renderer, font, hourText,
                 startX + hour * slotStep + slotStep / 3,
                 height * 21 / 100);

        SDL_Rect slot = {
            startX + hour * slotStep,
            height * 25 / 100,
            slotStep * 9 / 10,
            slotHeight
        };
        int meetingId = member->disponibilites[hour];
        if(meetingId == FREE_SLOT) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_RenderDrawRect(renderer, &slot);
            continue;
        }

        if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING)
            continue;

        Meeting* meeting = &sharedData->meetings[meetingId];
        if(meeting->priorityOrder == MAJOR)
            SDL_SetRenderDrawColor(renderer, 200, 60, 60, 255);
        else
            SDL_SetRenderDrawColor(renderer, 60, 120, 255, 255);
        SDL_RenderFillRect(renderer, &slot);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &slot);

        char label[16];
        snprintf(label, sizeof(label), "M%d", meetingId);
        drawText(renderer, font, label,
                 slot.x + slot.w / 6, slot.y + slot.h / 4);
    }
}

// Affiche l'agenda du membre selectionne et les options de demande de reunion.
static void drawMemberPage(SDL_Renderer* renderer, TTF_Font* font,
                           SharedData* sharedData, UIState* state,
                           int width, int height) {
    Member* member = getMemberById(sharedData, state->selectedMemberId);
    if(member == NULL) {
        drawText(renderer, font, "Membre introuvable",
                 width / 25, height / 5);
        return;
    }

    char title[64];
    snprintf(title, sizeof(title), "Membre %d - Equipe %d",
             member->memberId, member->teamId);
    drawText(renderer, font, title, width / 25, height * 10 / 100);
    drawButton(renderer, font, getBackButtonRect(width, height),
               "Retour equipe", 0);
    drawMemberAgenda(renderer, font, sharedData, member, width, height);

    SDL_Rect form = {
        width / 25, height * 42 / 100,
        width * 95 / 100, height * 54 / 100
    };
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &form);
    drawText(renderer, font, "Demander une reunion",
             form.x + form.w / 50, form.y + form.h / 30);

    drawText(renderer, font, "Priorite :", width / 20, height * 50 / 100);
    drawButton(renderer, font, getPriorityButtonRect(0, width, height),
               "Mineure", state->priority == MINOR);
    drawButton(renderer, font, getPriorityButtonRect(1, width, height),
               "Majeure", state->priority == MAJOR);

    drawText(renderer, font, "Duree :", width / 20, height * 58 / 100);
    for(int option = 0; option < 3; option++) {
        char durationText[8];
        snprintf(durationText, sizeof(durationText), "%dH", option + 1);
        drawButton(renderer, font, getDurationButtonRect(option, width, height),
                   durationText, state->duration == option + 1);
    }

    char startText[16];
    if(state->start == UNSPECIFIED_START)
        snprintf(startText, sizeof(startText), "AUTO");
    else
        snprintf(startText, sizeof(startText), "%dH", state->start + 8);
    drawText(renderer, font, "Debut :", width / 20, height * 66 / 100);
    drawButton(renderer, font, getStartButtonRect(0, width, height),
               "AUTO", state->start == UNSPECIFIED_START);
    drawButton(renderer, font, getStartButtonRect(1, width, height),
               "<", 0);
    drawButton(renderer, font, getStartButtonRect(2, width, height),
               startText, 0);
    drawButton(renderer, font, getStartButtonRect(3, width, height),
               ">", 0);

    drawText(renderer, font, "Participants :", width / 20, height * 75 / 100);
    char participantTeamLabel[24];
    snprintf(participantTeamLabel, sizeof(participantTeamLabel),
             "Equipe %d", state->participantTeam + 1);
    drawButton(renderer, font, getParticipantTeamButtonRect(0, width, height),
               "<", 0);
    drawButton(renderer, font, getParticipantTeamButtonRect(1, width, height),
               participantTeamLabel, 0);
    drawButton(renderer, font, getParticipantTeamButtonRect(2, width, height),
               ">", 0);

    int buttonIndex = 0;
    if(state->participantTeam >= 0 && state->participantTeam < NB_EQUIPES) {
        Team* team = &sharedData->teams[state->participantTeam];
        for(int i = 0; i < MAX_NB_MEMBRES; i++) {
            Member* candidate = &team->members[i];
            if(candidate->memberId <= 0)
                continue;

            char label[16];
            snprintf(label, sizeof(label), "M%d", candidate->memberId);
            drawButton(renderer, font,
                       getParticipantButtonRect(buttonIndex, width, height),
                       label, hasParticipant(state, candidate->memberId));
            buttonIndex++;
        }
    }

    drawButton(renderer, font, getSubmitButtonRect(width, height),
               "Demander", 0);
    if(state->status[0] != '\0')
        drawText(renderer, font, state->status,
                 width / 20, height * 87 / 100);
}

// Met a jour le message de retour affiche dans le formulaire.
static void setStatus(UIState* state, const char* text) {
    snprintf(state->status, sizeof(state->status), "%s", text);
}

// Initialise une nouvelle demande avec le membre courant comme participant.
static void resetMeetingForm(UIState* state, int memberId) {
    state->priority = MINOR;
    state->duration = 1;
    state->start = UNSPECIFIED_START;
    state->participantCount = 1;
    state->participantIds[0] = memberId;
    state->status[0] = '\0';
}

// Retire un participant en decalant les identifiants suivants dans le tableau.
static void removeParticipant(UIState* state, int participantIndex) {
    for(int i = participantIndex; i < state->participantCount - 1; i++)
        state->participantIds[i] = state->participantIds[i + 1];
    state->participantCount--;
}

// Ajoute ou retire un participant; le membre qui porte la demande reste inclus.
static void toggleParticipant(UIState* state, int memberId) {
    for(int i = 0; i < state->participantCount; i++) {
        if(state->participantIds[i] == memberId) {
            if(memberId != state->selectedMemberId)
                removeParticipant(state, i);
            return;
        }
    }

    if(state->participantCount < SIZE_BIGGEST_ROOM)
        state->participantIds[state->participantCount++] = memberId;
}

// Transmet la demande au gestionnaire via requestMeeting et affiche le resultat d'envoi.
static int submitMeetingRequest(int msqid, UIState* state) {
    if(state->participantCount < 1 ||
       requestMeeting(msqid, state->selectedMemberId, state->priority,
                      state->start, state->duration,
                      state->participantCount, state->participantIds) == -1) {
        setStatus(state, "Echec de l'envoi de la demande");
        return -1;
    }

    setStatus(state, "Demande envoyee au gestionnaire");
    return 0;
}

// Envoie une commande de suppression au gestionnaire.
// La suppression effective et la mise a jour des agendas sont gerees par celui-ci.
static void deleteSelectedMeeting(int msqid, SharedData* sharedData,
                                  UIState* state) {
    if(state->selectedRoom < 0 || state->selectedRoom >= NB_ROOM ||
       state->selectedHour < 0 || state->selectedHour >= NB_HOURES_IN_A_DAY)
        return;

    int meetingId =
        sharedData->rooms[state->selectedRoom].disponibilites[state->selectedHour];
    if(meetingId < 0 || meetingId >= MAX_AMOUNT_MEETING)
        return;

    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MANAGER_REQUEST_TYPE;
    msg.requestType = DELETE_MEETING;
    msg.meeting.meetingId = meetingId;

    if(msgsnd(msqid, &msg, MSG_SIZE, IPC_NOWAIT) == -1) {
        perror("msgsnd");
        setStatus(state, "Impossible d'envoyer la suppression");
        return;
    }

    state->selectedRoom = -1;
    state->selectedHour = -1;
    setStatus(state, "Suppression demandee");
}

// Traduit les clics souris en navigation ou en actions sur la page courante.
static void handleClick(SDL_Event* event, int msqid,
                        SharedData* sharedData, UIState* state,
                        int width, int height) {
    if(event->type != SDL_MOUSEBUTTONDOWN ||
       event->button.button != SDL_BUTTON_LEFT)
        return;

    SDL_Point mouse = {event->button.x, event->button.y};
    SDL_Rect homeButton = getHomeButtonRect(width, height);
    SDL_Rect teamsButton = getTeamsButtonRect(width, height);
    SDL_Rect historyButton = getHistoryButtonRect(width, height);

    if(SDL_PointInRect(&mouse, &homeButton)) {
        state->page = PAGE_HOME;
        return;
    }
    if(SDL_PointInRect(&mouse, &teamsButton)) {
        state->page = PAGE_TEAMS;
        return;
    }
    if(SDL_PointInRect(&mouse, &historyButton)) {
        state->page = PAGE_HISTORY;
        return;
    }

    // Sur l'accueil, un clic selectionne un creneau ou demande la suppression
    // de la reunion affichee dans le panneau de details.
    if(state->page == PAGE_HOME) {
        SDL_Rect deleteButton = getDeleteButtonRect(width, height);
        if(state->selectedRoom >= 0 &&
           SDL_PointInRect(&mouse, &deleteButton)) {
            deleteSelectedMeeting(msqid, sharedData, state);
            return;
        }

        for(int room = 0; room < NB_ROOM; room++) {
            for(int hour = 0; hour < NB_HOURES_IN_A_DAY; hour++) {
                SDL_Rect slot = getSlotRect(room, hour, width, height);
                if(SDL_PointInRect(&mouse, &slot)) {
                    state->selectedRoom = room;
                    state->selectedHour = hour;
                    state->status[0] = '\0';
                    return;
                }
            }
        }
        return;
    }

    // La selection d'un membre initialise son formulaire avant d'ouvrir sa page.
    if(state->page == PAGE_TEAMS) {
        for(int teamIndex = 0; teamIndex < NB_EQUIPES; teamIndex++) {
            SDL_Rect teamButton = getTeamButtonRect(teamIndex, width, height);
            if(SDL_PointInRect(&mouse, &teamButton)) {
                state->selectedTeam = teamIndex;
                return;
            }
        }

        if(state->selectedTeam < 0 || state->selectedTeam >= NB_EQUIPES)
            return;

        Team* team = &sharedData->teams[state->selectedTeam];
        int displayedIndex = 0;
        for(int i = 0; i < MAX_NB_MEMBRES; i++) {
            Member* member = &team->members[i];
            if(member->memberId <= 0)
                continue;

            SDL_Rect memberButton = getMemberButtonRect(displayedIndex, width, height);
            if(SDL_PointInRect(&mouse, &memberButton)) {
                state->selectedMemberId = member->memberId;
                state->selectedTeam = member->teamId - 1;
                resetMeetingForm(state, member->memberId);
                state->participantTeam = member->teamId - 1;
                state->page = PAGE_MEMBER;
                return;
            }
            displayedIndex++;
        }
        return;
    }

    if(state->page != PAGE_MEMBER)
        return;

    SDL_Rect backButton = getBackButtonRect(width, height);
    if(SDL_PointInRect(&mouse, &backButton)) {
        state->page = PAGE_TEAMS;
        state->status[0] = '\0';
        return;
    }

    Member* member = getMemberById(sharedData, state->selectedMemberId);
    if(member == NULL)
        return;

    // Les controles suivants modifient les parametres de la demande de reunion.
    for(int option = 0; option < 2; option++) {
        SDL_Rect button = getPriorityButtonRect(option, width, height);
        if(SDL_PointInRect(&mouse, &button)) {
            state->priority = option == 0 ? MINOR : MAJOR;
            return;
        }
    }

    for(int option = 0; option < 3; option++) {
        SDL_Rect button = getDurationButtonRect(option, width, height);
        if(SDL_PointInRect(&mouse, &button)) {
            state->duration = option + 1;
            // Conserve un debut valide si la nouvelle duree ne tient plus
            // dans la journee a partir de l'heure actuellement choisie.
            if(state->start != UNSPECIFIED_START &&
               state->start > NB_HOURES_IN_A_DAY - state->duration)
                state->start = NB_HOURES_IN_A_DAY - state->duration;
            return;
        }
    }

    SDL_Rect autoButton = getStartButtonRect(0, width, height);
    if(SDL_PointInRect(&mouse, &autoButton)) {
        state->start = UNSPECIFIED_START;
        return;
    }

    SDL_Rect previousButton = getStartButtonRect(1, width, height);
    if(SDL_PointInRect(&mouse, &previousButton)) {
        if(state->start == UNSPECIFIED_START)
            state->start = 0;
        else if(state->start > 0)
            state->start--;
        return;
    }

    SDL_Rect nextButton = getStartButtonRect(3, width, height);
    if(SDL_PointInRect(&mouse, &nextButton)) {
        int lastStart = NB_HOURES_IN_A_DAY - state->duration;
        if(state->start == UNSPECIFIED_START)
            state->start = lastStart;
        else if(state->start < lastStart)
            state->start++;
        return;
    }

    SDL_Rect previousTeam = getParticipantTeamButtonRect(0, width, height);
    if(SDL_PointInRect(&mouse, &previousTeam)) {
        if(state->participantTeam > 0)
            state->participantTeam--;
        return;
    }

    SDL_Rect nextTeam = getParticipantTeamButtonRect(2, width, height);
    if(SDL_PointInRect(&mouse, &nextTeam)) {
        if(state->participantTeam < NB_EQUIPES - 1)
            state->participantTeam++;
        return;
    }

    int teamIndex = state->participantTeam;
    if(teamIndex >= 0 && teamIndex < NB_EQUIPES) {
        Team* team = &sharedData->teams[teamIndex];
        int buttonIndex = 0;
        for(int i = 0; i < MAX_NB_MEMBRES; i++) {
            Member* candidate = &team->members[i];
            if(candidate->memberId <= 0)
                continue;

            SDL_Rect participantButton =
                getParticipantButtonRect(buttonIndex, width, height);
            if(SDL_PointInRect(&mouse, &participantButton)) {
                toggleParticipant(state, candidate->memberId);
                return;
            }
            buttonIndex++;
        }
    }

    SDL_Rect submitButton = getSubmitButtonRect(width, height);
    if(SDL_PointInRect(&mouse, &submitButton))
        submitMeetingRequest(msqid, state);
}

// Initialise SDL, execute la boucle d'evenements et de rendu, puis libere
// toutes les ressources graphiques avant de terminer le processus UI.
void ui(int msqid, SharedData* sharedData) {
    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Impossible d'initialiser SDL : %s\n", SDL_GetError());
        return;
    }

    if(TTF_Init() != 0) {
        fprintf(stderr, "Impossible d'initialiser SDL_ttf : %s\n", TTF_GetError());
        SDL_Quit();
        return;
    }

    TTF_Font* font =
        TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 24);
    if(font == NULL) {
        fprintf(stderr, "Impossible de charger la police : %s\n", TTF_GetError());
        TTF_Quit();
        SDL_Quit();
        return;
    }

    SDL_DisplayMode displayMode;
    if(SDL_GetCurrentDisplayMode(0, &displayMode) != 0) {
        fprintf(stderr, "Impossible de lire la resolution : %s\n", SDL_GetError());
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    int width = displayMode.w * 3 / 4;
    int height = displayMode.h * 3 / 4;
    if(width < 800)
        width = 800;
    if(height < 600)
        height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "Agenda SR70", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        fprintf(stderr, "Impossible de creer la fenetre : %s\n", SDL_GetError());
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return;
    }
    SDL_SetWindowMinimumSize(window, 800, 600);

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(renderer == NULL) {
        fprintf(stderr, "Impossible de creer le rendu : %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    UIState state;
    memset(&state, 0, sizeof(state));
    state.page = PAGE_HOME;
    state.selectedRoom = -1;
    state.selectedHour = -1;
    state.selectedTeam = 0;
    state.priority = MINOR;
    state.duration = 1;
    state.start = UNSPECIFIED_START;

    int running = 1;
    // Chaque iteration traite les evenements recus puis redessine la page active.
    while(running) {
        SDL_GetWindowSize(window, &width, &height);
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) {
                running = 0;
            }
            else if(event.type == SDL_MOUSEWHEEL &&
                    state.page == PAGE_HISTORY) {
                state.historyScroll -= event.wheel.y;
            }
            else {
                if(lockSharedData(sharedData) == -1) {
                    running = 0;
                    break;
                }
                handleClick(&event, msqid, sharedData, &state, width, height);
                if(unlockSharedData(sharedData) == -1) {
                    running = 0;
                    break;
                }
            }
        }

        if(!running)
            break;

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        drawNavigation(renderer, font, state.page, width, height);

        if(lockSharedData(sharedData) == -1) {
            running = 0;
            break;
        }

        switch(state.page) {
            case PAGE_HOME:
                drawHomePage(renderer, font, sharedData, &state, width, height);
                break;
            case PAGE_MEMBER:
                drawMemberPage(renderer, font, sharedData, &state, width, height);
                break;
            case PAGE_HISTORY:
                drawHistoryPage(renderer, font, sharedData, &state, width, height);
                break;
            case PAGE_TEAMS:
                drawTeamsPage(renderer, font, sharedData, &state, width, height);
                break;
        }

        if(unlockSharedData(sharedData) == -1) {
            running = 0;
            break;
        }

        // Presente a l'ecran l'image construite pour cette iteration.
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
}
