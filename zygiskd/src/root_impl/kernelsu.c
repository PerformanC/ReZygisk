#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include <android/log.h>

#include "common.h"
#include "../utils.h"

#define LOG_TAG "ReZygisk-KernelSU"

// KernelSU socket path
#define KSU_SOCKET_PATH "/dev/socket/ksud"

// Cache for UID checks to reduce system calls
#define UID_CACHE_SIZE 32
struct uid_cache_entry {
    uid_t uid;
    bool granted_root;
    bool should_umount;
    bool is_manager;
    time_t timestamp;
};

static struct uid_cache_entry uid_cache[UID_CACHE_SIZE];
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Cache timeout in seconds
#define CACHE_TIMEOUT 60

// Initialize the cache
static void init_uid_cache(void) {
    pthread_mutex_lock(&cache_mutex);
    memset(uid_cache, 0, sizeof(uid_cache));
    for (int i = 0; i < UID_CACHE_SIZE; i++) {
        uid_cache[i].uid = -1; // Invalid UID
    }
    pthread_mutex_unlock(&cache_mutex);
}

// Find entry in cache or return NULL if not found
static struct uid_cache_entry* find_in_cache(uid_t uid) {
    pthread_mutex_lock(&cache_mutex);
    time_t now = time(NULL);
    
    for (int i = 0; i < UID_CACHE_SIZE; i++) {
        if (uid_cache[i].uid == uid) {
            // Check if cache entry is still valid
            if (now - uid_cache[i].timestamp < CACHE_TIMEOUT) {
                struct uid_cache_entry* entry = &uid_cache[i];
                pthread_mutex_unlock(&cache_mutex);
                return entry;
            } else {
                // Entry expired, mark as invalid
                uid_cache[i].uid = -1;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

// Add entry to cache
static void add_to_cache(uid_t uid, bool granted_root, bool should_umount, bool is_manager) {
    pthread_mutex_lock(&cache_mutex);
    
    // Find an empty slot or the oldest entry
    int oldest_idx = 0;
    time_t oldest_time = time(NULL);
    
    for (int i = 0; i < UID_CACHE_SIZE; i++) {
        if (uid_cache[i].uid == -1) {
            oldest_idx = i;
            break;
        }
        
        if (uid_cache[i].timestamp < oldest_time) {
            oldest_time = uid_cache[i].timestamp;
            oldest_idx = i;
        }
    }
    
    // Update the cache entry
    uid_cache[oldest_idx].uid = uid;
    uid_cache[oldest_idx].granted_root = granted_root;
    uid_cache[oldest_idx].should_umount = should_umount;
    uid_cache[oldest_idx].is_manager = is_manager;
    uid_cache[oldest_idx].timestamp = time(NULL);
    
    pthread_mutex_unlock(&cache_mutex);
}

// Optimized KernelSU socket communication with timeout and error handling
static int ksu_request(const char* request, char* response, size_t response_size) {
    if (!request || !response || response_size == 0) {
        return -1;
    }
    
    // Initialize response buffer
    memset(response, 0, response_size);
    
    // Create socket
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOGE("Failed to create socket: %s\n", strerror(errno));
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 2;  // 2 seconds timeout
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Connect to KernelSU socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, KSU_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Failed to connect to KernelSU socket: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    
    // Send request
    if (send(sockfd, request, strlen(request), 0) < 0) {
        LOGE("Failed to send request to KernelSU: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    
    // Receive response
    int bytes_received = recv(sockfd, response, response_size - 1, 0);
    if (bytes_received < 0) {
        LOGE("Failed to receive response from KernelSU: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    
    // Ensure null termination
    response[bytes_received] = '\0';
    
    // Close socket
    close(sockfd);
    
    return bytes_received;
}

// Setup KernelSU implementation
void kernelsu_setup(void) {
    LOGI("Setting up KernelSU implementation\n");
    
    // Initialize UID cache
    init_uid_cache();
    
    // Check if KernelSU socket exists
    struct stat st;
    if (stat(KSU_SOCKET_PATH, &st) != 0) {
        LOGE("KernelSU socket not found: %s\n", strerror(errno));
        return;
    }
    
    LOGI("KernelSU implementation initialized successfully\n");
}

// Check if a UID has been granted root access
bool ksu_uid_granted_root(uid_t uid) {
    // Check cache first
    struct uid_cache_entry* entry = find_in_cache(uid);
    if (entry) {
        return entry->granted_root;
    }
    
    // Prepare request
    char request[64];
    snprintf(request, sizeof(request), "uid_granted_root %d", uid);
    
    // Send request and get response
    char response[128];
    if (ksu_request(request, response, sizeof(response)) <= 0) {
        // Default to false on error
        add_to_cache(uid, false, false, false);
        return false;
    }
    
    // Parse response
    bool granted = (strcmp(response, "true") == 0);
    
    // Update cache
    add_to_cache(uid, granted, false, false); // We only know about root status for now
    
    return granted;
}

// Check if a UID should have mounts unmounted
bool ksu_uid_should_umount(uid_t uid) {
    // Check cache first
    struct uid_cache_entry* entry = find_in_cache(uid);
    if (entry) {
        return entry->should_umount;
    }
    
    // Prepare request
    char request[64];
    snprintf(request, sizeof(request), "uid_should_umount %d", uid);
    
    // Send request and get response
    char response[128];
    if (ksu_request(request, response, sizeof(response)) <= 0) {
        // Default to false on error
        return false;
    }
    
    // Parse response
    bool should_umount = (strcmp(response, "true") == 0);
    
    // If we have a cache entry, update it
    if (entry) {
        pthread_mutex_lock(&cache_mutex);
        entry->should_umount = should_umount;
        entry->timestamp = time(NULL);
        pthread_mutex_unlock(&cache_mutex);
    } else {
        // We don't have complete information for a new cache entry yet
        // Will be added when we have more information
    }
    
    return should_umount;
}

// Check if a UID is a manager app
bool ksu_uid_is_manager(uid_t uid) {
    // Check cache first
    struct uid_cache_entry* entry = find_in_cache(uid);
    if (entry) {
        return entry->is_manager;
    }
    
    // Prepare request
    char request[64];
    snprintf(request, sizeof(request), "uid_is_manager %d", uid);
    
    // Send request and get response
    char response[128];
    if (ksu_request(request, response, sizeof(response)) <= 0) {
        // Default to false on error
        return false;
    }
    
    // Parse response
    bool is_manager = (strcmp(response, "true") == 0);
    
    // If we have a cache entry, update it
    if (entry) {
        pthread_mutex_lock(&cache_mutex);
        entry->is_manager = is_manager;
        entry->timestamp = time(NULL);
        pthread_mutex_unlock(&cache_mutex);
    } else {
        // We don't have complete information for a new cache entry yet
        // Will be added when we have more information
    }
    
    return is_manager;
}
