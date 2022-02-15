#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

void* routine() {
    printf("Test from threads\n");
    sleep(3);
    printf("Thread closed\n");

    return NULL;
}

int main(int argc, char** argv) {

    int error = 0;

    pthread_t p1, p2;
    error |= pthread_create(&p1, NULL, &routine, NULL);
    error |= pthread_create(&p2, NULL, &routine, NULL);

    if (error) {
        printf("Problems with creating treads!\n");
        return EXIT_FAILURE;
    }
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);

    return EXIT_SUCCESS;
}