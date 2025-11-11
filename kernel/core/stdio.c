/* kernel/stdio.c */

/* Include your new header */
#include <kernel/stdio.h>

/* Include your TTY header for output functions */
#include <kernel/tty.h>
#include <kernel/serial.h>

/* This is a compiler-provided header for va_list */
#include <stdarg.h> 

/**
 * @brief Helper function to print an integer in a given base.
 * @param n The number to print.
 * @param base The base (e.g., 10 for decimal, 16 for hex).
 * @return Returns the number of characters printed.
 */
static int kernel_print_integer(long n, int base) {
    const char* digits = "0123456789abcdef";
    int count = 0;
    
    if (base == 10 && n < 0) {
        terminal_putchar('-');
        serial_putchar('-');
        n = -n;
        count++;
    }

    /* Recurse to print the most significant digits first */
    /* We use unsigned long to avoid overflow issues with -n */
    unsigned long un = (unsigned long)n;
    if (un / base) {
        count += kernel_print_integer(un / base, base);
    }
    
    /* Print the least significant digit */
    char ch = digits[un % base];
    terminal_putchar(ch);
    serial_putchar(ch);
    count++;
    
    return count;
}

/**
 * @brief Core formatted print implementation.
 */
int vprintf(const char* format, va_list args) {
    int count = 0;
    
    for (int i = 0; format[i] != '\0'; i++) {
        /* Handle non-format characters */
        if (format[i] != '%') {
            char ch = format[i];
            terminal_putchar(ch);
            serial_putchar(ch);
            count++;
            continue;
        }
        
        /* Handle the format specifier */
        i++;
        
        switch (format[i]) {
            case '\0':
                /* Reached end of string mid-specifier. */
                return count;
            
            case '%':
                /* Escaped percent sign */
                terminal_putchar('%');
                serial_putchar('%');
                count++;
                break;
            
            case 'c': {
                /* 'char' is promoted to 'int' when passed via ... */
                char c = (char)va_arg(args, int);
                terminal_putchar(c);
                serial_putchar(c);
                count++;
                break;
            }
                
            case 's': {
                const char* s = va_arg(args, const char*);
                if (s == NULL) {
                    s = "(null)";
                }
                /* Use your existing optimized function */
                     terminal_writestring(s);
                     serial_writestring(s);
                
                /* FIXME: We don't know how many chars were written
                   unless terminal_writestring returns it.
                   For now, we'll just not add to count.
                   A proper fix is to have terminal_writestring
                   return a 'size_t' or use strlen. */
                break;
            }

            case 'd': {
                int d = va_arg(args, int);
                count += kernel_print_integer(d, 10);
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                /* print as unsigned decimal */
                count += kernel_print_integer((long)u, 10);
                break;
            }
            
            case 'x':
            case 'X': {
                /* 'int' and 'unsigned int' are the same size */
                unsigned int x = va_arg(args, unsigned int);
                count += kernel_print_integer(x, 16);
                break;
            }
            
            case 'p': {
                /* A pointer. Print "0x" then the address in hex */
                void* p = va_arg(args, void*);
                terminal_writestring("0x");
                serial_writestring("0x");
                count += 2;
                count += kernel_print_integer((unsigned long)p, 16);
                break;
            }

            default:
                /* Unrecognized specifier, just print it */
                terminal_putchar('%');
                terminal_putchar(format[i]);
                serial_putchar('%');
                serial_putchar(format[i]);
                count += 2;
                break;
        }
    }
    
    return count;
}

/**
 * @brief Public wrapper for vprintf.
 */
int printf(const char* format, ...) {
    va_list args;
    
    /* Initialize args to point to the first arg after 'format' */
    va_start(args, format);
    
    /* Call the core logic */
    int count = vprintf(format, args);
    
    /* Clean up the va_list */
    va_end(args);
    
    return count;
}