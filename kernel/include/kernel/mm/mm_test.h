/* Memory subsystem self tests (run at boot) */

#ifndef _KERNEL_MM_MM_TEST_H
#define _KERNEL_MM_MM_TEST_H

/* Runs PMM, VMM and heap tests. Returns the number of failed checks. */
int mm_selftest(void);

#endif
