/**
 * @file signals.h
 * @brief Signal handling interface for graceful shutdown.
 *
 * Configures POSIX signal handlers using sigaction() to intercept termination
 * signals (SIGINT, SIGTERM) and facilitate clean worker thread exits.
 */

#ifndef SIGNALS_H
#define SIGNALS_H

/**
 * @brief Configures signal handlers for SIGINT (Ctrl+C) and SIGTERM.
 *
 * Utilizes the POSIX sigaction system call to ensure signal safety and
 * avoid race conditions during asynchronous notifications.
 */
void setup_signal_handlers(void);

#endif // SIGNALS_H
