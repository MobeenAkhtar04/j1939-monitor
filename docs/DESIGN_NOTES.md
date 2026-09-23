# Design notes (why things are the way they are)

These are the decisions most likely to come up in an interview, with the reasoning behind each.

**Why a `Sink` interface?** The decoding pipeline shouldn't know where its output goes. Serial, UDP, console, and the test recorder all implement one interface. Adding CAN re-transmission or a file writer later means no changes to `Monitor`. The end-to-end tests use a fake sink to inspect output.

**Why is the serial port non-blocking?** During development, a full serial buffer froze the whole decode loop. That is unacceptable on a vehicle, because one slow consumer must not stop fault detection. The logger now drops lines, counts them, and reports the count.

**Why does UDP use `MSG_DONTWAIT`?** Same reason. The dashboard is optional and must never apply back-pressure to the monitor.

**Why abort a BAM session on a bad sequence number instead of patching the gap?** A DM1 missing 7 bytes could decode into a wrong SPN or FMI. A wrong fault code is worse than no fault code. The session is discarded and the next periodic DM1 (1 s later) replaces it.

**Why is there hysteresis in Warning?** Without it, a temperature sitting right at 100 °C would flip between Warning and Normal on every reading ("chattering"). Leaving Warning requires dropping below 95 °C.

**Why is there a Recovery state instead of going straight back to Normal?** An intermittent fault shouldn't produce Fault→Normal→Fault flapping. Recovery requires 5 s of clean data, and any relapse goes straight back to Fault.

**Why is stale data a fault?** Losing contact with the engine ECU means you no longer know the engine's condition. Treating silence as "all good" is the classic unsafe failure. The timeout is 3 s, which is 3 × ET1's 1 s broadcast period.

**Why is `step()` separate from `update_*()`?** Inputs are latched first, then the rules are evaluated once. That makes the state machine deterministic and trivial to unit test: you set the inputs, call `step(t)`, and check the state. Time is passed in rather than read from a clock, so the tests control time completely.

**Why the `candump -L` format for traces?** It's the standard Linux CAN tool format. Traces work with `canplayer`/`candump`, and real bus captures can be replayed through the tests.
