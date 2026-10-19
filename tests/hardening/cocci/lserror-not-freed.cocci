// A bare LSError. luna-service2 allocates the message of every error it
// reports and the caller must LSErrorFree() it; CLSError (Lsutils.h) does
// that in its destructor, a bare LSError leaks on every failure path that
// forgets. Use CLSError.
@@
identifier e;
@@
* LSError e;
