#include "emsim.h"
#include <pthread.h>
#include <time.h>

#define DEBUG 0
#define debug_print(...) \
            do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)

//does work pool really need a mutex here, since different pointers are being referenced anyway? Exclusive write
struct work_pool {
    int* status;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

struct work_pool *workpool;

void update_status(int thr, int status) {
    pthread_mutex_lock(&workpool->mutex);
    workpool->status[thr] = status;
    pthread_mutex_unlock(&workpool->mutex);
}

struct playGroupMatch_data {
    // Parameters for playGroupMatch function
    int group_num;
    team_t* team1;
    team_t* team2;
    int* goals1;
    int* goals2;

    // Variable, which tells if the current job batch is done and thread can exit
    int status; 
    //thread needs to know its index to update threadpool
    int index;
    // avoid locking workpool, just use extra variable for data_set
    int data_set;

    // mutex and cv
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

void *playGroupMatch_thread(void *ptr) {
    struct playGroupMatch_data *my_data = (struct playGroupMatch_data*)ptr;
    struct timespec ts;
    debug_print("Thread %d initialized\n", my_data->index);

    // Used for cv timedwait


    /* Lock my data
     * wait for it to be updated
     * on update, check if status tells to finish, if so -> finish
     */
    debug_print("%d: locking data\n", my_data->index);
    pthread_mutex_lock(&my_data->mutex);
    // Wait for first task
    debug_print("%d: data locked\n", my_data->index);

    debug_print("%d: update to being free\n", my_data->index);
    update_status(my_data->index, 0);

    debug_print("%d: updated, send signal\n", my_data->index);
    pthread_cond_signal(&workpool->cond);

    debug_print("%d:waiting...\n", my_data->index);
    pthread_cond_wait(&my_data->cond, &my_data->mutex);
    while (my_data->status == 0) {
        //the actual computation
        debug_print("%d: got signal, start work\n", my_data->index);
        playGroupMatch(my_data->group_num, my_data->team1, my_data->team2, my_data->goals1, my_data->goals2);
        debug_print("%d: work done\n", my_data->index);

        debug_print("%d: update to being free\n", my_data->index);
        update_status(my_data->index, 0);
        my_data->data_set = 0;
        debug_print("%d: updated, send signal\n", my_data->index);
        pthread_cond_signal(&workpool->cond);

        debug_print("%d: waiting...\n", my_data->index);
        while (my_data->data_set == 0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2;
            pthread_cond_timedwait(&my_data->cond, &my_data->mutex, &ts);
        }
        debug_print("%d: status %d\n", my_data->index, my_data->status);
    }
    debug_print("%d: unlocking data\n", my_data->index);
    pthread_mutex_unlock(&my_data->mutex);
    debug_print("%d: data unlocked\n", my_data->index);
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

void find_idle_worker(int numWorker, int *idles, int *idlesNum) {
    // Read workpool status list and return first idle
    pthread_cond_wait(&workpool->cond, &workpool->mutex);
    int thr;
    *idlesNum = 0;
    for (thr = 0; thr < numWorker; thr++) {
        if (workpool->status[thr] == 0) {
            idles[*idlesNum] = thr;
            *idlesNum += 1;
        }
    }
}

void playGroups(team_t* teams, int numWorker) {
    debug_print("playGroups() started\n");
    // Definition list
    static const int TEAMS_PER_GROUP = NUMTEAMS / NUMGROUPS;
    int i, j, thr, g, numIdles;
    int *idles;
    int** goals;
    pthread_t *thread;
    struct playGroupMatch_data *match_data;


    idles = malloc(numWorker * sizeof(int));
    /*Goals approach:
     * Since teams can be concurrently read and the only values that need to be updated are goals and points
     * We can concurrently run and store goal results in matrix where each cell is accessed only once
     * <row_index> team goals scored to <column_index> team
     * matrix size NUMTEAMS * NUMTEAMS
     */
    goals = malloc(NUMTEAMS * sizeof(int*));
    for (i = 0; i < NUMTEAMS; i++) {
        goals[i] = malloc(NUMTEAMS * sizeof(int));
        for (j = 0; j < NUMTEAMS; j++) {
            goals[i][j] = 0;
        }
    }
    thread = malloc(numWorker * sizeof(*thread));
    match_data = malloc(numWorker * sizeof(*match_data));

    workpool = malloc(sizeof(struct work_pool));
    pthread_mutex_init(&workpool->mutex, NULL);
    pthread_cond_init(&workpool->cond, NULL);
    pthread_mutex_lock(&workpool->mutex);
    workpool->status = malloc(numWorker * sizeof(int));
    for (i = 0; i < numWorker; i++) {
        workpool->status[i] = 1;
    }
    
    
    debug_print("Initializing threads\n");
    // Start threads and keep them idling
    for (thr = 0; thr < numWorker; thr++) {
        match_data[thr].index = thr;
        match_data[thr].status = 0;
        pthread_mutex_init(&match_data[thr].mutex, NULL);
        pthread_cond_init(&match_data[thr].cond, NULL);
        pthread_create(thread + thr, NULL, &playGroupMatch_thread, match_data + thr);
    }

    debug_print("Distributing work to threads\n");
    for (g = 0; g < NUMGROUPS; ++g) {
        for (i =  g * TEAMS_PER_GROUP; i < (g+1) * TEAMS_PER_GROUP; ++i) {
            for (j = (g+1) * TEAMS_PER_GROUP - 1; j > i; --j) {
                debug_print("%d, %d, %d\n", g, i ,j);
                // debug_print("%d, %d, %d\n", g, i, j);
                find_idle_worker(numWorker, idles, &numIdles);
                debug_print("Found idle %d threads:\n", numIdles);
                for (thr = 0; thr < numIdles; thr++) {
                    debug_print("\t%d\n", idles[thr]);
                }
                
                //set thread as busy
                for (thr = 0; thr < numIdles; thr++) {
                    debug_print("Setting %d to busy\n", idles[thr]);
                    workpool->status[idles[thr]] = 1;
                    debug_print("Preparing data for %d thread\n", idles[thr]);
                    pthread_mutex_lock(&match_data[idles[thr]].mutex);
                    match_data[idles[thr]].group_num = g;
                    match_data[idles[thr]].team1 = &teams[i];
                    match_data[idles[thr]].team2 = &teams[j];
                    match_data[idles[thr]].goals1 = &goals[i][j];
                    match_data[idles[thr]].goals2 = &goals[j][i];
                    match_data[idles[thr]].data_set = 1;
                    pthread_mutex_unlock(&match_data[idles[thr]].mutex);
                    debug_print("Notifying the %d thread\n", idles[thr]);
                    pthread_cond_signal(&match_data[idles[thr]].cond);
                }
            }
        }
    }
    //do not care about the workpool from here
    pthread_mutex_unlock(&workpool->mutex);

    debug_print("Sending messages that job is done\n");
    // Tell all threads, that job is done
    for (thr = 0; thr < numWorker; thr++) {
        debug_print("SENDING JOB DONE to %d\n", thr);
        pthread_mutex_lock(&match_data[thr].mutex);
        match_data[thr].status = 1;
        match_data[thr].data_set = 1;
        pthread_mutex_unlock(&match_data[thr].mutex);
        pthread_cond_signal(&match_data[thr].cond);
    }

    debug_print("Synchronizing threads\n");
    // Synchronize
    for (thr = 0; thr < numWorker; thr++) {
        pthread_join(thread[thr], NULL);
    }

    debug_print("Linearly calculating goals and points\n");
    //Calculate goals and points
    for (i = 0; i < NUMTEAMS; i++) {
        //Made measurements - not worth parallelising.
        calc_goals_and_points(i, &teams[i], goals);
    }   

    debug_print("Cleaning up\n");
    // Clean up
    for (i = 0; i < NUMTEAMS; i++) {
        free(goals[i]);
    }

    for (i = 0; i < numWorker; i++) {
        pthread_mutex_destroy(&match_data[i].mutex);
        pthread_cond_destroy(&match_data[i].cond);
    }

    free(idles);
    free(goals);
    free(thread);
    free(match_data);

    free(workpool->status);
    pthread_mutex_destroy(&workpool->mutex);
    pthread_cond_destroy(&workpool->cond);
    free(workpool);
    debug_print("playGroups() finished\n");
}



void playFinalRound(int numGames, team_t** teams, team_t** successors, int numWorker) {
    team_t* team1;
    team_t* team2;
    int i, goals1 = 0, goals2 = 0;

    for (i = 0; i < numGames; ++i) {
        team1 = teams[i*2];
        team2 = teams[i*2+1];
        playFinalMatch(numGames, i, team1, team2, &goals1, &goals2);

        if (goals1 > goals2)
            successors[i] = team1;
        else if (goals1 < goals2)
            successors[i] = team2;
        else {
            playPenalty(team1, team2, &goals1, &goals2);
            if (goals1 > goals2)
                successors[i] = team1;
            else
                successors[i] = team2;
        }
    }
} 