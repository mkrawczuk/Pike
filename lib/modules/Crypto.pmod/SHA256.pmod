#pike __REAL_VERSION__
#pragma strict_types

//! SHA256 is another hash function specified by NIST, intended as a
//! replacement for @[SHA1], generating larger digests. It outputs hash
//! values of 256 bits, or 32 octets.

#if constant(Nettle) && constant(Nettle.SHA256_Info)

// NOTE: Depends on the order of INIT invocations.
inherit Nettle.SHA256_Info;
inherit .Hash;

.HashState `()() { return Nettle.SHA256_State(); }

#else
constant this_program_does_not_exist=1;
#endif
