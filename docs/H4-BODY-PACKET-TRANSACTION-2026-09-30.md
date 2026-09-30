# Halo 4 body packet transaction

The optional first-person body adjustment now runs as a post-producer packet
transaction. `Halo4FirstPersonProducerDetour` invokes the native producer once
through `halo4_body_packet::InvokeNativeOnceAndThen`; only after native rows
exist does the optional callback consider mutation. The callback identifies
exact local flag-0 body and flag-1 first-person hands rows by the retained H4
render-model identity resolver, rejects duplicate or missing rows, checks the
local owner before solving and again before commit, and builds the result from
a private hands preview plus a body before-image.

The transaction writes the body transform bank and both region masks only
after all checks succeed. Each attempted target is restored from its stock
before-image after any partial write failure. An incomplete rollback has its
own reported result; a guarded preview/read/owner failure leaves the producer's
stock packet intact. The optional producer callback catches its own structured
fault after native production, while a fault from the native producer itself
continues through the existing outer cleanup path.

`tests/halo4_body_packet_backend_tests.cpp` exercises exact local rows,
foreign ownership, duplicate rows, preview rejection, stale-pair rejection,
unreadable rows, each of the three partial-write positions, rollback failure,
invalid output counts, and the shared native-once/post-producer boundary. The
focused fixture passed 18 checks. This verifies the software transaction only;
the candidate DLL still needs the cumulative build and H4 headset validation.
