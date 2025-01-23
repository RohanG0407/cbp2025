# cbp2025
Championship Branch Prediction 2025

## Build
The simulator expects a branch predictor to be implemented in the files my_cond_branch_predictor.h/cc. The Makefile already accounts for these. The predictor is statically instantiated within the implemented file. For reference check sample predictor file: [my_cond_branch_predictor.h](./sample_branch_predictor/my_cond_branch_predictor.h)

To build the simulator once the predictor files are added:

`make clean && make`

## Sample Branch Predictor
The simulator comes with a sample branch-predictor in sample_branch_predictor(./sample_branch_predictor/). To build with the sample branch-predictor run:

`ln -s sample_branch_predictor/my_cond_branch_predictor.cc .`

`ln -s sample_branch_predictor/my_cond_branch_predictor.h .`

`make clean && make`

## Examples
See Simulator options:

`./cbp`

Running the simulator on `trace.gz`:

`./cbp trace.gz`

Running with periodic misprediction stats at every 1M instr(`-E <n>`)

`./cbp -E 1000000 trace.gz`

## Notes

Run `make clean && make` to ensure your changes are taken into account.

Sample traces are provided : [sample_traces](./sample_traces)

Script to run all traces and dump a csv is also provided : [trace_exec_training_list](scripts/trace_exec_training_list.py)

To run the script, update the trace_folder and  results dir inside the script and run:

`python trace_exec_training_list.py`

## Branch Predictor Interface

See [cbp.h](./cbp.h) and [cond_branch_predictor_interface.cc](./cond_branch_predictor_interface.cc)

## Getting Traces

TODO: Add wget calls

## Prediction Interfaces
TODO:: Elaborate about the interface

## Sample Output

```
WINDOW_SIZE = 1024
FETCH_WIDTH = 16
FETCH_NUM_BRANCH = 16
FETCH_STOP_AT_INDIRECT = 1
FETCH_STOP_AT_TAKEN = 1
FETCH_MODEL_ICACHE = 1
PERFECT_BRANCH_PRED = 0
PERFECT_INDIRECT_PRED = 1
PIPELINE_FILL_LATENCY = 10
NUM_LDST_LANES = 8
NUM_ALU_LANES = 16
MEMORY HIERARCHY CONFIGURATION---------------------
STRIDE Prefetcher = 1
PERFECT_CACHE = 0
WRITE_ALLOCATE = 1
Within-pipeline factors:
  AGEN latency = 1 cycle
  Store Queue (SQ): SQ size = window size, oracle memory disambiguation, store-load forwarding = 1 cycle after store's or load's agen.
  * Note: A store searches the L1$ at commit. The store is released
  * from the SQ and window, whether it hits or misses. Store misses
  * are buffered until the block is allocated and the store is
  * performed in the L1$. While buffered, conflicting loads get
  * the store's data as they would from the SQ.
I$: 128 KB, 8-way set-assoc., 64B block size
L1$: 128 KB, 8-way set-assoc., 64B block size, 3-cycle search latency
L2$: 4 MB, 8-way set-assoc., 64B block size, 12-cycle search latency
L3$: 32 MB, 16-way set-assoc., 128B block size, 50-cycle search latency
Main Memory: 150-cycle fixed search time
---------------------------STORE QUEUE MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)---------------------------
Number of loads: 417260
Number of loads that miss in SQ: 239412 (57.38%)
Number of PFs issued to the memory system 5411
---------------------------------------------------------------------------------------------------------------------------------------
------------------------MEMORY HIERARCHY MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------
I$:
  accesses   = 1313295
  misses     = 44
  miss ratio = 0.00%
  pf accesses   = 0
  pf misses     = 0
  pf miss ratio = -nan%
L1$:
  accesses   = 582033
  misses     = 79
  miss ratio = 0.01%
  pf accesses   = 5411
  pf misses     = 58
  pf miss ratio = 1.07%
L2$:
  accesses   = 123
  misses     = 123
  miss ratio = 100.00%
  pf accesses   = 58
  pf misses     = 58
  pf miss ratio = 100.00%
L3$:
  accesses   = 123
  misses     = 89
  miss ratio = 72.36%
  pf accesses   = 58
  pf misses     = 28
  pf miss ratio = 48.28%
---------------------------------------------------------------------------------------------------------------------------------------
----------------------------------------------Prefetcher (Full Simulation i.e. No Warmup)----------------------------------------------
Num Trainings :417260
Num Prefetches generated :5432
Num Prefetches issued :14837
Num Prefetches filtered by PF queue :52
Num untimely prefetches dropped from PF queue :21
Num prefetches not issued LDST contention :9426
Num prefetches not issued stride 0 :139934
---------------------------------------------------------------------------------------------------------------------------------------

-------------------------------ILP LIMIT STUDY (Full Simulation i.e. Counts Not Reset When Warmup Ends)--------------------------------
instructions = 997741
cycles       = 193123
CycWP        = 61660
IPC          = 5.1663

---------------------------------------------------------------------------------------------------------------------------------------

-----------------------------------------------BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)----------------------------------------------
Type                   NumBr     MispBr        mr     mpki
CondDirect           111265       1140   1.0246%   0.8680
JumpDirect            26868          0   0.0000%   0.0000
JumpIndirect              1          0   0.0000%   0.0000
JumpReturn            10589          0   0.0000%   0.0000
Not control         1164572          0   0.0000%   0.0000
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 10M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 25M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

---------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (50 Perc instructions)---------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

-------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. Counts Not Reset When Warmup Ends)-------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997741       193123   5.1663     111265       1140   0.5761       0.0059   1.0246%   1.1426      61660    54.0877    61.7996
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 Read 997741 instrs 

```

