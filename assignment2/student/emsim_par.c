#include "emsim.h"
#include <pthread.h>
#include <time.h>

#define DEBUG 
#define debug_print(...) \
            do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)

typedef struct {
    int groupNo;
    team_t * team1;
    team_t * team2;
    int * goals1;
    int * goals2;
} matchData;

typedef struct {
    int index;
    int ready;
    matchData *match;
    pthread_cond_t ccv; // Consumer conditional variable
    pthread_cond_t pcv; // Producer conditional variable
    pthread_mutex_t mutex;
} playGroupMatchData;

typedef struct {
    matchData * matches;
    int index;
    int numMatches;
    pthread_mutex_t mutex;
} matchQueue;

matchQueue *matchQ;

void getMatchData(playGroupMatchData * m) {
    pthread_mutex_lock(&matchQ->mutex);
    if (matchQ->index == matchQ->numMatches) {
        m->match = NULL;
    } else {
        m->match = &matchQ->matches[matchQ->index];
        matchQ->index += 1;
    }
    pthread_mutex_unlock(&matchQ->mutex);
}

void * playGroupsConsumer(void * ptr) {
    playGroupMatchData * our_data = (playGroupMatchData *)ptr;
    pthread_mutex_lock(&our_data->mutex);
    debug_print("C%d: Data locked\n", our_data->index);
    if (our_data->ready != 0) {
        pthread_cond_signal(&our_data->pcv);
    } else {
        our_data->ready = 1;
    }
    pthread_cond_wait(&our_data->ccv, &our_data->mutex);
    while (our_data->match != NULL) {
        debug_print("C%d: Running match: %s vs %s\n", our_data->index, our_data->match->team1->name, our_data->match->team2->name);
        playGroupMatch(our_data->match->groupNo, our_data->match->team1, our_data->match->team2, 
                       our_data->match->goals1, our_data->match->goals2);
        pthread_cond_signal(&our_data->pcv);
        pthread_cond_wait(&our_data->ccv, &our_data->mutex);
    }
    debug_print("C%d: done, unlocking data\n", our_data->index);
    pthread_cond_signal(&our_data->pcv);
    pthread_mutex_unlock(&our_data->mutex);
    return NULL;
}

void *playGroupsProducer(void * ptr) {
    playGroupMatchData * our_data = (playGroupMatchData *)ptr;
    pthread_mutex_lock(&our_data->mutex);
    debug_print("P%d: Data lock acquired\n", our_data->index);
    if (our_data->ready == 0) {
        debug_print("P%d: first to lock data, waiting for consumer...\n", our_data->index);
        our_data->ready = 1;
        pthread_cond_wait(&our_data->pcv, &our_data->mutex);
    }
    debug_print("P%d: consumer ready, get data\n", our_data->index);
    do {
        getMatchData(our_data);
        pthread_cond_signal(&our_data->ccv);
        pthread_cond_wait(&our_data->pcv, &our_data->mutex);
    } while (our_data->match != NULL);

    debug_print("P%d:matchQ empty, unlocking\n", our_data->index);
    pthread_mutex_unlock(&our_data->mutex);
    return NULL;
}

void calc_goals_and_points(int team_num, team_t *team, int **goals) {
    int i;
    for (i = 0; i < NUMTEAMS; i++) {
        team->goals += goals[team_num][i] - goals[i][team_num];
        if (goals[team_num][i] > goals[i][team_num]) {
            team->points += 3;
        } else if (goals[team_num][i] == goals[i][team_num]) {
            team->points += 1;
        }
    }
}

void playGroups(team_t* teams, int numWorker) {
    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    numWorker *= 2;
    static const int TEAMSPERGROUP = NUMTEAMS / NUMGROUPS;
    pthread_t *consumers;
    pthread_t *producers;
    int g, i, j, m,matchNum;
    playGroupMatchData *data;
    int ** goals; // Plant goals into matrix - doesn't block threads.

    consumers = malloc(numWorker / 2 * sizeof(*consumers));
    producers = malloc(numWorker / 2 * sizeof(*producers));
    matchQ = malloc(sizeof(*matchQ));
    data = malloc(numWorker / 2 * sizeof(*data));
    goals = malloc(NUMTEAMS * sizeof(int*));
    for (i = 0; i < NUMTEAMS; i++) {
        goals[i] = malloc(NUMTEAMS * sizeof(int));
        for (j = 0; j < NUMTEAMS; j++) {
            goals[i][j] = 0;
        }
    }
    
    // Triangular formula used for matches
    matchNum = NUMGROUPS * TEAMSPERGROUP * (TEAMSPERGROUP - 1) / 2;
    debug_print("Matches: %d\n", matchNum);
    pthread_mutex_init(&matchQ->mutex, NULL);
    matchQ->index = 0;
    matchQ->numMatches = matchNum;
    matchQ->matches = malloc(matchNum * sizeof(*matchQ->matches));
    m = 0;
    for (g = 0; g < NUMGROUPS; ++g) {
        for (i =  g * TEAMSPERGROUP; i < (g+1) * TEAMSPERGROUP; ++i) {
            for (j = (g+1) * TEAMSPERGROUP - 1; j > i; --j) {  
                debug_print("%d %d %d %d\n", g, i, j, m);
                matchQ->matches[m].groupNo = g;
                matchQ->matches[m].team1 = &teams[i];
                matchQ->matches[m].team2 = &teams[j];
                debug_print("\t%s vs %s\n", teams[i].name, teams[j].name);
                matchQ->matches[m].goals1 = &goals[i][j];
                matchQ->matches[m].goals2 = &goals[j][i];
                m += 1;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    debug_print("\n\tPreparation: %.5f seconds\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
                     ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));

    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (i = 0; i < (numWorker / 2); i++) {
        debug_print("Running %d/%d pair\n", i, numWorker / 2);
        pthread_cond_init(&(data + i)->ccv, NULL);
        pthread_cond_init(&(data + i)->pcv, NULL);
        pthread_mutex_init(&(data + i)->mutex, NULL);
        data[i].index = i;
        data[i].ready = 0;
        pthread_create(consumers + i, NULL, &playGroupsConsumer, data + i);
        pthread_create(producers + i, NULL, &playGroupsProducer, data + i);
    }

    for (i = 0; i < numWorker / 2; i++) {
        pthread_join(consumers[i], NULL);
        pthread_join(producers[i], NULL);
        pthread_cond_destroy(&(data + i)->ccv);
        pthread_cond_destroy(&(data + i)->pcv);
        pthread_mutex_destroy(&(data + i)->mutex);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    debug_print("\n\tRun: %.5f seconds\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
                     ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));

    // Assign points from goals
    for (i = 0; i < NUMTEAMS; i++) {
        //Made measurements - not worth parallelising.
        calc_goals_and_points(i, &teams[i], goals);
    }  

    clock_gettime(CLOCK_MONOTONIC, &begin);
    pthread_mutex_destroy(&matchQ->mutex);
    free(consumers);
    free(producers);
    free(matchQ->matches);
    free(matchQ);
    free(data);
    for (i = 0; i < NUMTEAMS; i++) {
        free(goals[i]);
    }
    free(goals);
    clock_gettime(CLOCK_MONOTONIC, &end);
    debug_print("\n\tClean-up: %.5f seconds\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
                     ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));
}


typedef struct {
    int numGames;
    int gameNo;
    team_t * team1;
    team_t * team2;
    team_t ** successors;
    int * goals1;
    int * goals2;
} roundData;

typedef struct {
    int index;
    int ready;
    roundData *r;
    pthread_cond_t ccv; // Consumer conditional variable
    pthread_cond_t pcv; // Producer conditional variable
    pthread_mutex_t mutex;

} playFinalRoundData;

typedef struct {
    roundData * rounds;
    int index;
    int numMatches;
    pthread_mutex_t mutex;
} roundQueue;

roundQueue * roundQ;

void getRoundData(playFinalRoundData * m) {
    pthread_mutex_lock(&roundQ->mutex);
    if (roundQ->index == roundQ->numMatches) {
        m->r = NULL;
    } else {
        m->r = &roundQ->rounds[roundQ->index];
        roundQ->index += 1;
    }
    pthread_mutex_unlock(&roundQ->mutex);
}

void * playFinalRoundConsumer(void * ptr) {
    playFinalRoundData * our_data = (playFinalRoundData *)ptr;
    pthread_mutex_lock(&our_data->mutex);
    debug_print("C%d: Data locked\n", our_data->index);
    if (our_data->ready != 0) {
        debug_print("C%d: I'm second, signal producer\n", our_data->index);
        pthread_cond_signal(&our_data->pcv);
    } else {
        debug_print("C%d: I'm first, update ready\n", our_data->index);
        our_data->ready = 1;
    }
    pthread_cond_wait(&our_data->ccv, &our_data->mutex);
    while (our_data->r != NULL) {
        debug_print("C%d: Running match: %s vs %s\n", our_data->index, our_data->r->team1->name, our_data->r->team2->name);
        playFinalMatch(our_data->r->numGames, our_data->r->gameNo,
                       our_data->r->team1, our_data->r->team2,
                       our_data->r->goals1, our_data->r->goals2);
        if (*our_data->r->goals1 == *our_data->r->goals2) {
            debug_print("\tC%d: tie - play penalty\n", our_data->index);
            playPenalty(our_data->r->team1, our_data->r->team2, our_data->r->goals1, our_data->r->goals2);
        }

        if (*our_data->r->goals1 > *our_data->r->goals2) {
            our_data->r->successors[our_data->r->gameNo] = our_data->r->team1;
        } else {
            our_data->r->successors[our_data->r->gameNo] = our_data->r->team2;
        }
        debug_print("\tC%d: %s vs %s, winner: %s\n", our_data->index, our_data->r->team1->name,
                                                     our_data->r->team2->name, our_data->r->successors[our_data->r->gameNo]->name);
        debug_print("\tC%d: job done, signal producer\n", our_data->index);
        pthread_cond_signal(&our_data->pcv);
        pthread_cond_wait(&our_data->ccv, &our_data->mutex);
    }
    debug_print("C%d: done, unlocking data\n", our_data->index);
    pthread_cond_signal(&our_data->pcv);
    pthread_mutex_unlock(&our_data->mutex);
    return NULL;
}

void * playFinalRoundProducer(void * ptr) {
    playFinalRoundData * our_data = (playFinalRoundData *)ptr;
    pthread_mutex_lock(&our_data->mutex);
    debug_print("P%d: Data lock acquired\n", our_data->index);
    if (our_data->ready == 0) {
        debug_print("P%d: first to lock data, waiting for consumer...\n", our_data->index);
        our_data->ready = 1;
        pthread_cond_wait(&our_data->pcv, &our_data->mutex);
    }
    debug_print("P%d: consumer ready, get data\n", our_data->index);
    do {
        getRoundData(our_data);
        pthread_cond_signal(&our_data->ccv);
        pthread_cond_wait(&our_data->pcv, &our_data->mutex);
    } while (our_data->r != NULL);

    debug_print("P%d:roundQ empty, unlocking\n", our_data->index);
    pthread_mutex_unlock(&our_data->mutex);
    return NULL;
}

void playFinalRound(int numGames, team_t** teams, team_t** successors, int numWorker) {
    debug_print("playFinalRound start\n");
    struct timespec begin, end;
    numWorker *= 2;
    pthread_t *consumers;
    pthread_t *producers;
    int i, j;
    playFinalRoundData *data;
    int ** goals; 

    debug_print("Init data\n");
    consumers = malloc(numWorker / 2 * sizeof(*consumers));
    producers = malloc(numWorker / 2 * sizeof(*producers));
    data = malloc(numWorker / 2 * sizeof(*data));
    goals = malloc(2 * numGames * sizeof(int*));
    for (i = 0; i < 2 * numGames; i++) {
        goals[i] = malloc(2 * numGames * sizeof(int));
        for (j = 0; j < 2 * numGames; j++) {
            goals[i][j] = 0;
        }
    }
    debug_print("Games: %d\n", numGames);
    debug_print("Preparing data\n");
    roundQ = malloc(sizeof(*roundQ));
    pthread_mutex_init(&roundQ->mutex, NULL);
    roundQ->index = 0;
    roundQ->numMatches = numGames;
    roundQ->rounds = malloc(numGames * sizeof(*roundQ->rounds));
    for (i = 0; i < numGames; ++i) {
        debug_print("%d", i);
        roundQ->rounds[i].numGames = numGames;
        roundQ->rounds[i].gameNo = i;
        roundQ->rounds[i].team1 = teams[i * 2]; 
        roundQ->rounds[i].team2 = teams[i * 2 + 1];
        debug_print("\t%s vs %s\n", teams[i * 2]->name, teams[i * 2 + 1]->name);
        roundQ->rounds[i].successors = successors;
        roundQ->rounds[i].goals1 = &goals[i * 2][i * 2 + 1];
        roundQ->rounds[i].goals2 = &goals[i * 2 + 1][i * 2];
    }
    debug_print("Running jobs\n");

    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (i = 0; i < (numWorker / 2); i++) {
        debug_print("Running %d/%d pair\n", i, numWorker / 2);
        pthread_cond_init(&(data + i)->ccv, NULL);
        pthread_cond_init(&(data + i)->pcv, NULL);
        pthread_mutex_init(&(data + i)->mutex, NULL);
        data[i].index = i;
        data[i].ready = 0;
        pthread_create(consumers + i, NULL, &playFinalRoundConsumer, data + i);
        pthread_create(producers + i, NULL, &playFinalRoundProducer, data + i);
    }

    debug_print("Wait for threads to finish\n");
    for (i = 0; i < (numWorker / 2); i++) {
        pthread_join(consumers[i], NULL);
        pthread_join(producers[i], NULL);
        pthread_cond_destroy(&(data + i)->ccv);
        pthread_cond_destroy(&(data + i)->pcv);
        pthread_mutex_destroy(&(data + i)->mutex);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    debug_print("\n\tRun: %.5f seconds\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
                     ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));
    

    pthread_mutex_destroy(&roundQ->mutex);
    free(consumers);
    free(producers);
    free(roundQ->rounds);
    free(roundQ);
    free(data);
    for (i = 0; i < 2 * numGames; i++) {
        free(goals[i]);
    }
    free(goals);
} 