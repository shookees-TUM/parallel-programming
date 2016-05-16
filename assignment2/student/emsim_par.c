#include "emsim.h"
#include <pthread.h>

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

    // mutex and cv
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

void *playGroupMatch_thread(void *ptr) {
    struct playGroupMatch_data *my_data = (struct playGroupMatch_data*)ptr;
    /* Lock my data
     * wait for it to be updated
     * on update, check if status tells to finish, if so -> finish
     */
    pthread_mutex_lock(&my_data->mutex);
    // Wait for first task
    pthread_cond_wait(&my_data->cond, &my_data->mutex);
    while (my_data->status == 0) {
        //the actual computation
        playGroupMatch(my_data->group_num, my_data->team1, my_data->team2, my_data->goals1, my_data->goals2);
        
        // Update thread_pool that the calculation is done
        update_status(my_data->index, 0);

        // If main thread is listening, it will prepare data for this thread
        pthread_cond_signal(&workpool->cond);
        pthread_cond_wait(&my_data->cond, &my_data->mutex);
    }

    pthread_mutex_unlock(&my_data->mutex);
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

int find_idle_worker(int numWorker) {
    // Read workpool status list and see if there's an idle thread
    int thr;
    pthread_mutex_lock(&workpool->mutex);
    for (thr = 0; thr < numWorker + 1; thr++) {
        if (thr < numWorker && workpool->status[thr] == 0)
            break;
    }
    pthread_mutex_unlock(&workpool->mutex);

    // Find first idle thread, +1 is used for getting thr higher than numWorker
    
    if (thr > numWorker) {
        //All threads busy, wait for pool condition variable
        pthread_mutex_lock(&workpool->mutex);
        pthread_cond_wait(&workpool->cond, &workpool->mutex);
        for (thr = 0; thr < numWorker + 1; thr++) {
            if (workpool->status[thr] == 0)
                break;
        }
        pthread_mutex_unlock(&workpool->mutex);
        
    }
    return thr;
}

void playGroups(team_t* teams, int numWorker) {
    // Definition list
    static const int TEAMS_PER_GROUP = NUMTEAMS / NUMGROUPS;
    int i, j, thr, g;
    int** goals;
    pthread_t *thread;
    struct playGroupMatch_data *match_data;

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
        workpool->status[i] = 0;
    }
    pthread_mutex_unlock(&workpool->mutex);
    
    // Start threads and keep them idling
    for (thr = 0; thr < numWorker; thr++) {
        match_data[thr].index = thr;
        match_data[thr].status = 0;
        pthread_mutex_init(&match_data[thr].mutex, NULL);
        pthread_cond_init(&match_data[thr].cond, NULL);
        pthread_create(thread + thr, NULL, &playGroupMatch_thread, match_data + thr);
    }

    for (g = 0; g < NUMGROUPS; ++g) {
        for (i = g * TEAMS_PER_GROUP; i < (g + 1) * TEAMS_PER_GROUP; ++i) {
            for (j = i + 1; j < (g + 1) * TEAMS_PER_GROUP; ++j) {
                // printf("%d, %d, %d\n", g, i, j);
                thr = find_idle_worker(numWorker);

                //set thread as busy
                update_status(thr, 1);

                // Set up data for <thr> thread
                pthread_mutex_lock(&match_data[thr].mutex);
                match_data[thr].group_num = g;
                match_data[thr].team1 = &teams[i];
                match_data[thr].team2 = &teams[j];
                match_data[thr].goals1 = &goals[i][j];
                match_data[thr].goals2 = &goals[j][i];
                pthread_mutex_unlock(&match_data[thr].mutex);
                pthread_cond_signal(&match_data[thr].cond);
            }
        }
    }

    // Tell all threads, that job is done
    for (thr = 0; thr < numWorker; thr++) {
        pthread_mutex_lock(&match_data[thr].mutex);
        match_data[thr].status = 1;
        pthread_mutex_unlock(&match_data[thr].mutex);
        pthread_cond_signal(&match_data[thr].cond);
    }

    // Synchronize
    for (thr = 0; thr < numWorker; thr++) {
        pthread_join(thread[thr], NULL);
    }

    //Calculate goals and points
    for (i = 0; i < NUMTEAMS; i++) {
        //Made measurements - not worth parallelising.
        calc_goals_and_points(i, &teams[i], goals);
    }   

    // Clean up
    for (i = 0; i < NUMTEAMS; i++) {
        free(goals[i]);
    }

    for (i = 0; i < numWorker; i++) {
        pthread_mutex_destroy(&match_data[i].mutex);
        pthread_cond_destroy(&match_data[i].cond);
    }

    free(goals);
    free(thread);
    free(match_data);

    free(workpool->status);
    pthread_mutex_destroy(&workpool->mutex);
    pthread_cond_destroy(&workpool->cond);
    free(workpool);
}

struct playFinalMatch_arg
{
    int numGames;
    int gameNo;
    team_t* team1;
    team_t* team2;
    int* goals1;
    int* goals2;
    team_t* successor;

    // Thread variables
    int index;
    int status;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

void * playFinalRound_thread(void *ptr)
{
    struct playFinalMatch_arg *my_data = (struct playFinalMatch_arg*)ptr;
    pthread_mutex_lock(&my_data->mutex);
    pthread_cond_wait(&my_data->cond, &my_data->mutex);
    while (my_data->status == 0) {
        printf("%d before match\n", my_data->index);
        playFinalMatch(my_data->numGames, my_data->gameNo, my_data->team1, my_data->team2, my_data->goals1, my_data->goals2);
        printf("%d after match\n", my_data->index);
        if (my_data->goals1 > my_data->goals2) {
            my_data->successor = my_data->team1;
        } else if (my_data->goals1 < my_data->goals2) {
            my_data->successor = my_data->team2;
        } else {
            // Tied
            playPenalty(my_data->team1, my_data->team2, my_data->goals1, my_data->goals2);
            if (my_data->goals1 > my_data->goals2) {
                my_data->successor = my_data->team1;
            } else {
                my_data->successor = my_data->team2;
            }
        }
        update_status(my_data->index, 0);
        pthread_cond_signal(&workpool->cond);
        pthread_cond_wait(&my_data->cond, &my_data->mutex);
    }
    pthread_mutex_unlock(&my_data->mutex);
    return NULL;
}

void playFinalRound(int numGames, team_t** teams, team_t** successors, int numWorker) {
    int i, thr = 0;
    int *goals1;
    int *goals2;
    pthread_t *thread;

    struct playFinalMatch_arg *arg;
    //printf("Threads %d\n", numWorker);
    thread = malloc(numWorker * sizeof(*thread));
    arg = malloc(numWorker * sizeof(*arg));
    goals1 = malloc(numWorker * sizeof(*goals1));
    goals2 = malloc(numWorker * sizeof(*goals2));

    workpool = malloc(sizeof(struct work_pool));
    pthread_mutex_init(&workpool->mutex, NULL);
    pthread_cond_init(&workpool->cond, NULL);
    pthread_mutex_lock(&workpool->mutex);
    workpool->status = malloc(numWorker * sizeof(int));
    for (i = 0; i < numWorker; i++) {
        workpool->status[i] = 0;
    }
    pthread_mutex_unlock(&workpool->mutex);
    
    // Start threads and keep them idling
    for (thr = 0; thr < numWorker; thr++) {
        arg[thr].index = thr;
        arg[thr].status = 0;
        pthread_mutex_init(&arg[thr].mutex, NULL);
        pthread_cond_init(&arg[thr].cond, NULL);
        pthread_create(thread + thr, NULL, &playFinalRound_thread, arg + thr);
    }
    printf("final\n");
    for (i = 0; i < numGames; i++) {
        thr = find_idle_worker(numWorker);

        //set thread as busy
        update_status(thr, 1);

        // Set up data for <thr> thread
        pthread_mutex_lock(&arg[thr].mutex);
        arg[thr].numGames = numGames;
        arg[thr].gameNo = i;
        arg[thr].team1 = teams[2 * i];
        arg[thr].team2 = teams[2 * i + 1];
        arg[thr].goals1 = &goals1[thr];
        arg[thr].goals2 = &goals2[thr];
        arg[thr].successor = successors[thr];
        pthread_mutex_unlock(&arg[thr].mutex);
        pthread_cond_signal(&arg[thr].cond);
    }

    printf("after for cycle\n");
    // Tell all threads, that job is done
    for (thr = 0; thr < numWorker; thr++) {
        pthread_mutex_lock(&arg[thr].mutex);
        arg[thr].status = 1;
        pthread_mutex_unlock(&arg[thr].mutex);
        pthread_cond_signal(&arg[thr].cond);
    }

    // Synchronize
    for (thr = 0; thr < numWorker; thr++) {
        pthread_join(thread[thr], NULL);
    }

    for (thr = 0; thr < numWorker; thr++) {
        pthread_mutex_destroy(&arg[thr].mutex);
        pthread_cond_destroy(&arg[thr].cond);
    }

    free(thread);
    free(arg);
    free(goals1);
    free(goals2);

    free(workpool->status);
    pthread_mutex_destroy(&workpool->mutex);
    pthread_cond_destroy(&workpool->cond);
    free(workpool);
} 
