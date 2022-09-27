/**
 * @brief Fork a process
 *
 * @param s
 * @param regs Invoking thread's register context
 * @return tid of newly created thread, -1 on error
 */
int scheduler_fork_process(ureg_t *regs)
{
    tcb_t *current_tcb = NODE2TCB(s.current_tcb_node);
    pcb_t *current_pcb = current_tcb->pcb;

    lock_acquire(&s.pcb_lock);
    if (current_tcb->pcb->num_threads > 1)
    {
        disable_interrupts();
        lock_release(&s.pcb_lock);
        enable_interrupts();

        return -1;
    }
    disable_interrupts();
    lock_release(&s.pcb_lock);
    enable_interrupts();

    pcb_t *new_pcb = malloc(sizeof(pcb_t));
    if (!new_pcb)
    {
        goto pcb_malloc_failed;
    }

    tcb_t *new_tcb = malloc(sizeof(tcb_t));
    if (!new_tcb)
    {
        goto tcb_malloc_failed;
    }

    if (pcb_init(new_pcb))
    {
        lprintf("In scheduler_fork_process, pcb_init failed!");
        goto pcb_init_failed;
    }
    new_pcb->parent_pcb = current_pcb;

    int tid = fetch_and_add((uint32_t *)&s.next_tid, 1);
    if (tcb_init(new_tcb, new_pcb, tid, RUNNABLE, regs))
    {
        lprintf("In scheduler_fork_process, tcb_init failed!");
        goto tcb_init_failed;
    }

    lock_acquire(&s.malloc_lock);
    if (hashtable_insert(&s.tid_tcb_node_map,
                         (void *)tid, (void *)&new_tcb->tcb_node))
    {
        disable_interrupts();
        lock_release(&s.malloc_lock);
        enable_interrupts();

        lprintf("In scheduler_fork_process, hashtable_insert failed!");
        goto hashtable_insert_failed;
    }

    disable_interrupts();
    lock_release(&s.malloc_lock);
    enable_interrupts();

    // no need to acquire vm lock because we assume only one thread operating
    if (vm_duplicate_address_space(
            &current_pcb->vm_control_block, &new_pcb->vm_control_block))
    {
        lprintf("In scheduler_fork_process, vm_dup_address_space failed!");
        goto failed;
    }

    sim_reg_child(
        (void *)new_pcb->vm_control_block.page_directory_base_address,
        (void *)current_pcb->vm_control_block.page_directory_base_address);

    pcb_add_running_child(current_pcb, new_tcb->tid, new_pcb);

    disable_interrupts();
    add_tcb_into_queue(&new_tcb->tcb_node, &s.runnable_queue);
    enable_interrupts();

    return new_tcb->tid;

failed:
    lock_acquire(&s.malloc_lock);
    hashtable_find(&s.tid_tcb_node_map, (void *)tid, 1);

    disable_interrupts();
    lock_release(&s.malloc_lock);
    enable_interrupts();
hashtable_insert_failed:
    tcb_destroy(new_tcb);
tcb_init_failed:
    pcb_destroy(new_pcb);
pcb_init_failed:
    free(new_tcb);
tcb_malloc_failed:
    free(new_pcb);
pcb_malloc_failed:
    return -1;
}