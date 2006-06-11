// random_state constructor.

// General includes.
#include "cl_sysdep.h"

// Specification.
#include "cln/random.h"


// Implementation.

#include "cl_base_config.h"
#include "cl_low.h"

#if defined(unix) || defined(__unix) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(_AIX) || defined(sinix) || (defined(__MACH__) && defined(__APPLE__)) || (defined(_WIN32) && defined(__GNUC__)) || defined(__BEOS__)

#include <sys/types.h>
#include <unistd.h> // declares getpid()
#include <cstdlib>  // declares rand()

#if defined(HAVE_GETTIMEOFDAY)

#include <sys/time.h>
#ifdef GETTIMEOFDAY_DOTS
  extern "C" int gettimeofday (struct timeval * tp, ...);
#else
  extern "C" int gettimeofday (struct timeval * tp, GETTIMEOFDAY_TZP_T tzp);
#endif

inline uint32 get_seed (void)
{
	var struct timeval tv;
	gettimeofday(&tv,0);
	return cln::highlow32(tv.tv_sec,tv.tv_usec); // 16+16 zuf�llige Bits
}

#elif defined(HAVE_TIMES_CLOCK)

#include <ctime>
#ifndef CLK_TCK
#include <sys/time.h>
#endif
#include <sys/times.h>
extern "C" clock_t times (struct tms * buffer);

inline uint32 get_seed (void)
{
	var struct tms tmsbuf;
	var uint32 seed_lo = times(&tmsbuf);
	return seed_lo + tmsbuf.tms_utime + tmsbuf.tms_stime;
}

#endif

#endif

namespace cln {

// Counter, to avoid that two random-states created immediately one after
// the other contain the same seed.
static uint32 counter = 0;

random_state::random_state ()
{
	var uint32 seed_hi;
	var uint32 seed_lo;
#if defined(unix) || defined(__unix) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(_AIX) || defined(sinix) || (defined(__MACH__) && defined(__APPLE__)) || (defined(_WIN32) && defined(__GNUC__)) || defined(__BEOS__)
	seed_lo = ::get_seed();
	seed_hi = (rand() // zuf�llige 31 Bit (bei UNIX_BSD) bzw. 16 Bit (bei UNIX_SYSV)
                          << 8) ^ (uintL)(getpid()); // ca. 8 Bit von der Process ID
#elif defined(__OpenBSD__)
	seed_lo = arc4random();
	seed_hi = arc4random();
#elif defined(__atarist)
	seed_lo = highlow32(GEMDOS_GetDate(),GEMDOS_GetTime()); // 16+16 zuf�llige Bits
	seed_hi = XBIOS_Random(); // 24 Bit zuf�llig vom XBIOS, vorne 8 Nullbits
#elif defined(amiga) || defined(AMIGA)
	seed_lo = get_real_time(); // Uhrzeit
	seed_hi = FindTask(NULL); // Pointer auf eigene Task
#elif defined(__MSDOS__) || defined(__EMX__) || defined(__riscos)
	// Keine Zufallszahlen, keine PID, nichts Zuf�lliges da.
	seed_lo = get_real_time(); // Uhrzeit, 100 Hz
	seed_hi = time(NULL);
#else
#error "Must implement random_state constructor!"
#endif
	seed_hi ^= counter++ << 5;
	seed.hi = seed_hi;
	seed.lo = seed_lo;
}

}  // namespace cln
