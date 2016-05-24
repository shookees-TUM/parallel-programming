# Parallelization plan
[TOC]

## General approach

 1. Main thread - single producer,
 2. `numWorker` universal worker threads that are used for group and final games,
 3. worker data struct has mode flag for differentiating between group and final games,
 4. Don't use lazy buffer (like in assignment solution)
 5. Buffer - FIFO


## `playGroups()` parallelization

 1. Wait for consumer if the buffer is full
 2. fill buffer with match information and signal that new work is available

## `playFinalRound()` parallelization