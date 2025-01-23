# cbp2025
Championship Branch Prediction 2025

## Examples
See Simulator options:

`./cbp`

Running the simulator on `trace.gz`:

`./cbp trace.gz`

Running with periodic misprediction stats at every 1M instr(`-E <n>`)

`./cbp -E 1000000 trace.gz`

## Notes

Run `make clean && make` to ensure your changes are taken into account.

Sample traces are provided : [traces_trial](./traces_trial)

Script to run all traces and dump a csv is also provided : [trace_exec_training_list](scripts/trace_exec_training_list.py)

To run the script, update the trace_folder and  results dir inside the script and run:
`python trace_exec_training_list.py`

## Branch Predictor Interface

See [cbp.h](./cbp.h) and [cond_branch_predictor_interface.cc](./cond_branch_predictor_interface.cc)

## Getting Traces

TODO: Add wget calls


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
STORE QUEUE MEASUREMENTS (Full Simulation i.e. No Warmup)---------------------------
Number of loads: 325732
Number of loads that miss in SQ: 198990 (61.09%)
Number of PFs issued to the memory system 5341
MEMORY HIERARCHY MEASUREMENTS (Full Simulation i.e. No Warmup)----------------------
I$:
  accesses   = 1138403
  misses     = 310
  miss ratio = 0.03%
  pf accesses   = 0
  pf misses     = 0
  pf miss ratio = -nan%
L1$:
  accesses   = 536590
  misses     = 1537
  miss ratio = 0.29%
  pf accesses   = 5341
  pf misses     = 475
  pf miss ratio = 8.89%
L2$:
  accesses   = 1847
  misses     = 1847
  miss ratio = 100.00%
  pf accesses   = 475
  pf misses     = 475
  pf miss ratio = 100.00%
L3$:
  accesses   = 1847
  misses     = 1507
  miss ratio = 81.59%
  pf accesses   = 475
  pf misses     = 239
  pf miss ratio = 50.32%
Prefetcher (Full Simulation i.e. No Warmup)------------------------------------------
Num Trainings :325732
Num Prefetches generated :5341
Num Prefetches issued :5776
Num Prefetches filtered by PF queue :0
Num untimely prefetches dropped from PF queue :0
Num prefetches not issued LDST contention :435
Num prefetches not issued stride 0 :172169

------------------------------------ILP LIMIT STUDY (Full Simulation i.e. No Warmup)------------------------------------
instructions = 997301
cycles       = 338310
WPCycles     = 33348
IPC          = 2.9479
------------------------------------------------------------------------------------------------------------------------

-----------------------------------------------------------BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. No Warmup)----------------------------------------------------------
Type                   NumBr     MispBr        mr     mpki
CondDirect           128874        264   0.2049%   0.2319
JumpDirect            25846          0   0.0000%   0.0000
JumpIndirect          14255          0   0.0000%   0.0000
JumpReturn            12902          0   0.0000%   0.0000
Not control          956526          0   0.0000%   0.0000
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 10M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997301       338310   2.9479     128874        264   0.3809       0.0008   0.2049%   0.2647      33348   126.3182    33.4382
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Last 25M instructions)-----------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997301       338310   2.9479     128874        264   0.3809       0.0008   0.2049%   0.2647      33348   126.3182    33.4382
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

---------------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (50 Perc instructions)---------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997301       338310   2.9479     128874        264   0.3809       0.0008   0.2049%   0.2647      33348   126.3182    33.4382
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

-------------------------------------------------DIRECT CONDITIONAL BRANCH PREDICTION MEASUREMENTS (Full Simulation i.e. No Warmup)-------------------------------------------------
       Instr       Cycles      IPC      NumBr     MispBr BrPerCyc MispBrPerCyc        MR     MPKI      CycWP   CycWPAvg   CycWPPKI
      997301       338310   2.9479     128874        264   0.3809       0.0008   0.2049%   0.2647      33348   126.3182    33.4382
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

```

