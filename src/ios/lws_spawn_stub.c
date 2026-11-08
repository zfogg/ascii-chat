/* The iOS client never launches subprocesses.  libwebsockets' basic spawn
 * implementation still references this process-tree sampler symbol. */
void lws_spawn_tree_sample_cb(void *sul) {
    (void)sul;
}
