#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/time.h>

#include <android/log.h>

#include "companion.h"
#include "utils.h"

#define LOG_TAG "ReZygisk-Companion"

// Timeout for companion operations
#define COMPANION_TIMEOUT 10 // 10 seconds

// Flag to indicate if companion is shutting down
static volatile sig_atomic_t companion_shutdown = 0;

// Signal handler for companion shutdown
static void handle_companion_signal(int sig) {
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Companion received signal %d, initiating shutdown\n", sig);
  companion_shutdown = 1;
}

// Setup signal handlers for the companion
static void setup_companion_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_companion_signal;
  sigemptyset(&sa.sa_mask);
  
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  
  // Ignore SIGPIPE to prevent crashes when writing to closed sockets
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

// Function to load module with improved error handling
static zygisk_companion_entry load_module(int library_fd) {
  if (library_fd < 0) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Invalid library file descriptor: %d\n", library_fd);
    return NULL;
  }
  
  // Set a timeout for loading the module
  alarm(COMPANION_TIMEOUT);
  
  // Use a unique name for each module to prevent conflicts
  char module_path[256];
  snprintf(module_path, sizeof(module_path), "/proc/self/fd/%d", library_fd);
  
  // Load the module
  void *handle = dlopen(module_path, RTLD_NOW);
  
  // Cancel the timeout
  alarm(0);
  
  if (!handle) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to load module: %s\n", dlerror());
    return NULL;
  }
  
  // Get the entry point
  zygisk_companion_entry entry = (zygisk_companion_entry)dlsym(handle, "zygisk_companion_entry");
  if (!entry) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to find entry point: %s\n", dlerror());
    dlclose(handle);
    return NULL;
  }
  
  return entry;
}

// Function to handle client connection with improved error handling and resource management
static void handle_client(int client_fd, zygisk_companion_entry entry) {
  if (client_fd < 0 || !entry) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Invalid client fd or entry point\n");
    return;
  }
  
  // Set socket timeout to prevent hanging
  struct timeval tv;
  tv.tv_sec = 2;  // 2 seconds timeout
  tv.tv_usec = 0;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  
  // Set socket to non-blocking mode
  int flags = fcntl(client_fd, F_GETFL, 0);
  fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  
  // Call the module entry point with timeout protection
  alarm(COMPANION_TIMEOUT);
  entry(client_fd);
  alarm(0);
  
  // Check memory usage after handling client
  // check_memory_usage(); // This function is not defined in the provided code
}

// Main companion entry point with improved resource management
void companion_entry(int fd) {
  // Setup signal handlers
  setup_companion_signals();
  
  // Set low priority to reduce battery usage
  // set_low_priority(); // This function is not defined in the provided code
  
  // Load the module
  zygisk_companion_entry module_entry = load_module(fd);
  if (!module_entry) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to load companion module\n");
    close(fd);
    return;
  }
  
  // Create a socket pair for communication
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to create socket pair: %s\n", strerror(errno));
    close(fd);
    return;
  }
  
  // Send one end of the socket pair back to the daemon
  if (write_fd(fd, sockets[0]) == -1) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to send socket to daemon: %s\n", strerror(errno));
    close(sockets[0]);
    close(sockets[1]);
    close(fd);
    return;
  }
  
  // Close the daemon connection and the sent socket
  close(fd);
  close(sockets[0]);
  
  // Keep the other end for communication with clients
  int module_socket = sockets[1];
  
  // Main event loop with improved polling
  while (!companion_shutdown) {
    // Use poll instead of select for better performance
    struct pollfd pfd;
    pfd.fd = module_socket;
    pfd.events = POLLIN;
    
    // Poll with timeout to allow for periodic checks
    int poll_ret = poll(&pfd, 1, 1000); // 1 second timeout
    
    if (poll_ret < 0) {
      if (errno == EINTR) continue; // Interrupted by signal, retry
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "poll failed: %s\n", strerror(errno));
      break;
    }
    
    if (poll_ret == 0) {
      // Timeout, perform periodic tasks
      continue;
    }
    
    if (pfd.revents & POLLIN) {
      // Accept client connection
      int client_fd = read_fd(module_socket);
      if (client_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to read client fd: %s\n", strerror(errno));
        continue;
      }
      
      // Handle client request
      handle_client(client_fd, module_entry);
      
      // Close client socket
      close(client_fd);
    }
  }
  
  // Cleanup resources
  close(module_socket);
  
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Companion shutdown complete\n");
}
