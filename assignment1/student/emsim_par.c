#include "emsim.h"
#include <pthread.h>

//workaround for playFinalRound not having a parameter for thread amount
int THREADSNUM = 0; 

struct playGroup_args
{
  int groupNo;
  team_t* teams;
  int numTeams;
  team_t** first;
  team_t** second;
  team_t** third;
};

/*struct playGroup_ret
{
  team_t** first;
  team_t** second;
  team_t** third;
};*/

/** unpacks arguments and executes function
 * ptr - pointer to playGroup_args structured data
 */
void * playGroup_thread(void *ptr)
{
  struct playGroup_args *arg = (struct playGroup_args*)ptr;
  // printf("Group number: %d\n", arg->groupNo);
  playGroup(arg->groupNo,
            arg->teams,
            arg->numTeams,
            arg->first,
            arg->second,
            arg->third);
  // create struct for returned data
  /*struct playGroup_ret *ret;
  ret = malloc(sizeof(*ret));
  ret->first = arg->first;
  ret->second = arg->second;
  ret->third = arg->third;
  return ret;*/
  return NULL;
}

void playEM(team_t* teams, int numThreads)
{
  printf("threads: %d\n", numThreads);
  static const int cInitialNumSuccessors = NUMGROUPS * 2 + NUMTHIRDS;
  static const int cTeamsPerGroup = NUMTEAMS / NUMGROUPS;
  team_t* successors[NUMGROUPS * 2 + NUMTHIRDS] = {0};
  team_t* bestThirds[NUMGROUPS];
  int g, t, i, curBestThird = 0, numSuccessors = cInitialNumSuccessors;
  pthread_t *thread;
  struct playGroup_args *arg;
  // struct timespec begin, end;
  THREADSNUM = numThreads;

  thread = malloc(numThreads * sizeof(*thread));
  arg = malloc(numThreads * sizeof(*arg));

  initialize();
  // clock_gettime(CLOCK_MONOTONIC, &begin);
  // play groups - biggest throttle (60% runtime here)
  // threading strategy - two cycles:
  // 1. thread fullfill dispenser
  // 2. numThreads size for cycle to assign tasks
  for (g = 0, t = 0; g < NUMGROUPS; ++g, ++t)
  {
    if (t == numThreads)
    {
      // wait for threads to finish
      t = 0;
      for (i = 0; i < numThreads; i++)
      {
        pthread_join(thread[i], NULL);
      }
    }
    arg[t].groupNo = g;
    arg[t].teams = teams + (g * cTeamsPerGroup);
    arg[t].numTeams = cTeamsPerGroup;
    arg[t].first = successors + g * 2;
    arg[t].second = successors + (numSuccessors - (g * 2) - 1);
    arg[t].third = bestThirds + g;
    pthread_create(thread + t, NULL, &playGroup_thread, arg + t);
  }

if (t != 0)
  {
    // wait for threads to finish
    for (i = 0; i < t; i++)
      {
        pthread_join(thread[i], NULL);
      }
  }

  // clock_gettime(CLOCK_MONOTONIC, &end);
  // printf("\n\nplayGroup %d times: %.5f seconds\n", NUMGROUPS, ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
  //                    ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));

  // fill best thirds
  sortTeams(NUMGROUPS, bestThirds);
  for (g = 0; g < numSuccessors; ++g) {
    if (successors[g] == NULL) {
      successors[g] = bestThirds[curBestThird++];
    }
  }

  //threads won't be needed here anymore
  free(thread);
  free(arg);

  // clock_gettime(CLOCK_MONOTONIC, &begin);
  while (numSuccessors > 1) {
    playFinalRound(numSuccessors / 2, successors, successors);
    numSuccessors /= 2;
  }
  // clock_gettime(CLOCK_MONOTONIC, &end);
  // printf("\n\nplayFinalRound times: %.5f seconds\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
  //                    ((double)begin.tv_sec + 1.0e-9*begin.tv_nsec));

  
}

struct playFinalMatch_arg
{
  int numGames;
  int gameNo;
  team_t* team1;
  team_t* team2;
  int* goals1;
  int* goals2;
  team_t** successors;
};

void * playFinalRound_thread(void *ptr)
{
  struct playFinalMatch_arg *arg = (struct playFinalMatch_arg*)ptr;
  printf("Game number: %d, thread: %li\n", arg->gameNo, pthread_self());
  playFinalMatch(arg->numGames, arg->gameNo, arg->team1, arg->team2, arg->goals1, arg->goals2);

  if (*(arg->goals1) > *(arg->goals2))
      arg->successors[arg->gameNo] = arg->team1;    
    else if (*(arg->goals1) < *(arg->goals2))
      arg->successors[arg->gameNo] = arg->team2;
    else
    {
      playPenalty(arg->team1, arg->team2, arg->goals1, arg->goals2);
      if (*(arg->goals1) > *(arg->goals2))
        arg->successors[arg->gameNo] = arg->team1;
      else
        arg->successors[arg->gameNo] = arg->team2;
    }
  return NULL;
}

void playFinalRound(int numGames, team_t** teams, team_t** successors)
{
  int i, t, j = 0;
  int numThreads = THREADSNUM;
  int *goals1;
  int *goals2;
  pthread_t *thread;
  struct playFinalMatch_arg *arg;
  printf("Threads %d\n", numThreads);
  thread = malloc(numThreads * sizeof(*thread));
  arg = malloc(numThreads * sizeof(*arg));
  goals1 = malloc(numThreads * sizeof(*goals1));
  goals2 = malloc(numThreads * sizeof(*goals2));

  for (i = 0, t = 0; i < numGames; ++i, ++t) 
  {
    if (t == numThreads)
    {
        for (j = 0; j < numThreads; j++)
        {
          pthread_join(thread[j], NULL);
        }
        t = 0;
    } 
    arg[t].numGames = numGames;
    arg[t].gameNo = i;
    arg[t].team1 = teams[i * 2];
    arg[t].team2 = teams[i * 2 + 1];
    arg[t].goals1 = &goals1[t];
    arg[t].goals2 = &goals2[t];
    arg[t].successors = successors;
    pthread_create(thread + t, NULL, &playFinalRound_thread, arg + t);
  }
  printf("t = %d\n", t);
  if (t != 0)
  {
    for (j = 0; j < t; j++)
    {
        pthread_join(thread[j], NULL);
    }
  }
  free(thread);
  free(arg);
  free(goals1);
  free(goals2);
}
