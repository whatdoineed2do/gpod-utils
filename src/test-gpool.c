#include <glib.h>


#define MAX_THREADS 10

struct args {
  union {
    int id;
    int pending;
  } u;
};

G_LOCK_DEFINE(pending);

void thread(gpointer args_, gpointer pool_args_)
{
    g_usleep(rand()%2000000);
    struct args*  arg = (struct args*)args_;

    g_print("%p - %i\n", g_thread_self(), arg->u.id);
    free(arg);

    G_LOCK(pending);
    --(((struct args*)pool_args_)->u.pending);
    G_UNLOCK(pending);
}

int main(int argc, char *argv[])
{
    struct args  pool_args;
    pool_args.u.pending = MAX_THREADS;

    GThreadPool* tp = g_thread_pool_new((GFunc)thread, (void*)&pool_args,
                                         argc == 1 ? (int)(MAX_THREADS/3) : (int)atoi(argv[1]),
                                         TRUE, NULL);
    g_print("main - starting threads, pool threads %d\n", g_thread_pool_get_max_threads(tp));

    for(int i=0; i<MAX_THREADS; ++i)
    {
        struct args*  arg = malloc(sizeof(struct args));
        arg->u.id = i;
        g_thread_pool_push(tp, (void*)arg, NULL);
    }

    g_print("main - wait on (%d/%d) threads to complete\n", pool_args.u.pending, MAX_THREADS);
    g_thread_pool_free(tp, FALSE, TRUE);
    g_print("main - done (%d)\n", pool_args.u.pending);
    return 0;
}
