when i run ascii-chat server it prints zero logs and deadlocks.

(lldb) thread backtrace all
* thread #1, name = 'ascii-chat', stop reason = signal SIGSTOP
  * frame #0: 0x00007ffff4ea27a0 libc.so.6`___lldb_unnamed_symbol_93760 + 64
    frame #1: 0x00007ffff4eac2ce libc.so.6`__pthread_rwlock_wrlock + 462
    frame #2: 0x00007ffff6429a37 libasciichat.so.0`rwlock_wrlock_impl(lock=0x00007ffff72b0e38) at rwlock.c:55:10
    frame #3: 0x00007ffff6a01571 libasciichat.so.0`debug_create_and_insert_lock_record(lock_address=0x00007ffff6cbdd80, lock_type=LOCK_TYPE_MUTEX, file_name="/home/zfogg/src/github.com/zfogg/ascii-chat/include/ascii-chat/platform/init.h", line_number=143, function_name="static_mutex_lock") at lock.c:774:5
    frame #4: 0x00007ffff6a0104c libasciichat.so.0`debug_mutex_lock(mutex=0x00007ffff6cbdd80, file_name="/home/zfogg/src/github.com/zfogg/ascii-chat/include/ascii-chat/platform/init.h", line_number=143, function_name="static_mutex_lock") at lock.c:996:3
    frame #5: 0x00007ffff658ccbf libasciichat.so.0`static_mutex_lock(m=0x00007ffff6cbdd80) at init.h:143:3
    frame #6: 0x00007ffff658c72f libasciichat.so.0`init_default_luminance_palette at ascii_simd.c:41:3
    frame #7: 0x00007ffff658dda6 libasciichat.so.0`ascii_simd_init at ascii_simd.c:102:3
    frame #8: 0x00005555556be589 ascii-chat`server_main at main.c:1774:3
    frame #9: 0x00005555556bcfc3 ascii-chat`main(argc=2, argv=0x00007fffffffa128) at main.c:598:19
    frame #10: 0x00007ffff4e36635 libc.so.6`___lldb_unnamed_symbol_275c0 + 117
    frame #11: 0x00007ffff4e366e9 libc.so.6`__libc_start_main + 137
    frame #12: 0x00005555556b7465 ascii-chat`_start + 37
  thread #2, name = 'ascii-chat', stop reason = vforkdone
    frame #0: 0x00007ffff4f299fd libc.so.6`___lldb_unnamed_symbol_11a9e0 + 29
    frame #1: 0x00007ffff4f11c8b libc.so.6`___lldb_unnamed_symbol_102a80 + 523
    frame #2: 0x00007ffff4f1254b libc.so.6`___lldb_unnamed_symbol_103520 + 43
    frame #3: 0x00007ffff4f1132f libc.so.6`posix_spawn + 15
    frame #4: 0x00007ffff4e91637 libc.so.6`_IO_proc_open + 711
    frame #5: 0x00007ffff4e91968 libc.so.6`popen + 104
    frame #6: 0x00007ffff6451377 libasciichat.so.0`run_llvm_symbolizer_batch(buffer=0x00007bffdc5f2b60, size=6) at symbols.c:624:16
    frame #7: 0x00007ffff644d9c4 libasciichat.so.0`symbol_cache_resolve_batch(buffer=0x00007bffdb600820, size=9) at symbols.c:838:18
    frame #8: 0x00007ffff6467303 libasciichat.so.0`platform_backtrace_symbols(buffer=0x00007bffdb600820, size=9) at system.c:611:10
    frame #9: 0x00007ffff6468b78 libasciichat.so.0`platform_print_backtrace(skip_frames=1) at system.c:766:22
    frame #10: 0x00007ffff6a20a4e libasciichat.so.0`check_long_held_locks at lock.c:449:5
    frame #11: 0x00007ffff69f870c libasciichat.so.0`debug_thread_func(arg=0x0000000000000000) at lock.c:468:5
    frame #12: 0x00007ffff749e16d libclang_rt.asan-x86_64.so`___lldb_unnamed_symbol_9e0d0 + 157
    frame #13: 0x00007ffff4ea598b libc.so.6`___lldb_unnamed_symbol_966a0 + 747
    frame #14: 0x00007ffff4f29a0c libc.so.6`___lldb_unnamed_symbol_11aa05 + 7
(lldb)
ascii-chat │ signal SIGSTOP
