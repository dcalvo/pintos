#ifndef THREADS_FP_H
#define THREADS_FP_H

/* Definitions from B.6 Fixed Point Arithmetic down below.
https://cs.jhu.edu/~huang/cs318/fall20/project/pintos_8.html#SEC148 
*/


#define F 16384

/* Convert an integer n to fixed point representation. */
#define CONVERT_INT_TO_FP(n) (n * F)

/* Convert fixed point number x to an integer. */
#define CONVERT_FP_TO_INT(x) (x / F)

/* Convert fixed point number x to an integer (rounding to nearest). */
#define CONVERT_FP_TO_NEAR_INT(x) (x >= 0) ? ((x + F / 2) / F) : ((x - F/ 2) / F)

/* Add two fixed point numbers. */
#define ADD(x, y) (x + y)

/* Subtract two fixed point numbers. */
#define SUB(x, y) (x - y)

/* Multiply two fixed point numbers. */
#define MULT_FP(x, y) (((int64_t) x) * y / F)

/* Multiply an integer with a fixed point number. */
#define MULT_INTFP(x, n) (x * n)

/* Divide two fixed point numbers. */
#define DIV_FP(x, y) (((int64_t) x) * F / y)

/* Divide a fixed point number with an integer. */
#define DIV_INTFP(x, n) (x / n)

#endif