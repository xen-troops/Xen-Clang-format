/******************************************************************************
 * domain.c
 *
 * Generic domain-handling functions.
 */

#include <xsm/xsm.h>
#include <xen/trace.h>
#include <xen/tmem.h>
#include <asm/setup.h>

/* Linux config option: propageted to domain0 */
/* xen_processor_pmbits: xen control Cx, Px, ... */
unsigned int xen_processor_pmbits = XEN_PROCESSOR_PM_PX;

/* opt_dom0_vcpus_pin: If true, dom0 VCPUs are pinned. */
bool_t opt_dom0_vcpus_pin;
boolean_param( "dom0_vcpus_pin", opt_dom0_vcpus_pin );

/* Protect updates/reads (resp.) of domain_list and domain_hash. */
DEFINE_SPINLOCK( domlist_update_lock );
DEFINE_RCU_READ_LOCK( domlist_read_lock );

#define DOMAIN_HASH_SIZE 256
#define DOMAIN_HASH( _id ) ( ( int )( _id ) & ( DOMAIN_HASH_SIZE - 1 ) )
static struct domain* domain_hash[DOMAIN_HASH_SIZE];
struct domain* domain_list;

struct domain* hardware_domain __read_mostly;

#ifdef CONFIG_LATE_HWDOM
domid_t hardware_domid __read_mostly;
integer_param( "hardware_dom", hardware_domid );
#endif

struct vcpu* idle_vcpu[NR_CPUS] __read_mostly;

vcpu_info_t dummy_vcpu_info;

static void __domain_finalise_shutdown( struct domain* d )
{
    struct vcpu* v;

    BUG_ON( !spin_is_locked( &d->shutdown_lock ) );

    if ( d->is_shut_down )
        return;

    for_each_vcpu( d, v ) if ( !v->paused_for_shutdown ) return;

    d->is_shut_down = 1;
    if ( ( d->shutdown_code == SHUTDOWN_suspend ) && d->suspend_evtchn )
        evtchn_send( d, d->suspend_evtchn );
    else
        send_global_virq( VIRQ_DOM_EXC );
}

static void vcpu_check_shutdown( struct vcpu* v )
{
    struct domain* d = v->domain;

    spin_lock( &d->shutdown_lock );

    if ( d->is_shutting_down )
    {
        if ( !v->paused_for_shutdown )
            vcpu_pause_nosync( v );
        v->paused_for_shutdown = 1;
        v->defer_shutdown = 0;
        __domain_finalise_shutdown( d );
    }

    spin_unlock( &d->shutdown_lock );
}

static void vcpu_info_reset( struct vcpu* v )
{
    struct domain* d = v->domain;

    v->vcpu_info =
        ( ( v->vcpu_id < XEN_LEGACY_MAX_VCPUS )
              ? ( vcpu_info_t* )&shared_info( d, vcpu_info[v->vcpu_id] )
              : &dummy_vcpu_info );
    v->vcpu_info_mfn = mfn_x( INVALID_MFN );
}

struct vcpu*
alloc_vcpu( struct domain* d, unsigned int vcpu_id, unsigned int cpu_id )
{
    struct vcpu* v;

    BUG_ON( ( !is_idle_domain( d ) || vcpu_id ) && d->vcpu[vcpu_id] );

    if ( ( v = alloc_vcpu_struct() ) == NULL )
        return NULL;

    v->domain = d;
    v->vcpu_id = vcpu_id;

    spin_lock_init( &v->virq_lock );

    tasklet_init( &v->continue_hypercall_tasklet, NULL, 0 );

    grant_table_init_vcpu( v );

    if ( !zalloc_cpumask_var( &v->cpu_hard_affinity ) ||
         !zalloc_cpumask_var( &v->cpu_hard_affinity_tmp ) ||
         !zalloc_cpumask_var( &v->cpu_hard_affinity_saved ) ||
         !zalloc_cpumask_var( &v->cpu_soft_affinity ) ||
         !zalloc_cpumask_var( &v->vcpu_dirty_cpumask ) )
        goto fail_free;

    if ( is_idle_domain( d ) )
    {
        v->runstate.state = RUNSTATE_running;
    }
    else
    {
        v->runstate.state = RUNSTATE_offline;
        v->runstate.state_entry_time = NOW();
        set_bit( _VPF_down, &v->pause_flags );
        vcpu_info_reset( v );
        init_waitqueue_vcpu( v );
    }

    if ( sched_init_vcpu( v, cpu_id ) != 0 )
        goto fail_wq;

    if ( vcpu_initialise( v ) != 0 )
    {
        sched_destroy_vcpu( v );
    fail_wq:
        destroy_waitqueue_vcpu( v );
    fail_free:
        free_cpumask_var( v->cpu_hard_affinity );
        free_cpumask_var( v->cpu_hard_affinity_tmp );
        free_cpumask_var( v->cpu_hard_affinity_saved );
        free_cpumask_var( v->cpu_soft_affinity );
        free_cpumask_var( v->vcpu_dirty_cpumask );
        free_vcpu_struct( v );
        return NULL;
    }

    d->vcpu[vcpu_id] = v;
    if ( vcpu_id != 0 )
    {
        int prev_id = v->vcpu_id - 1;
        while ( ( prev_id >= 0 ) && ( d->vcpu[prev_id] == NULL ) )
            prev_id--;
        BUG_ON( prev_id < 0 );
        v->next_in_list = d->vcpu[prev_id]->next_in_list;
        d->vcpu[prev_id]->next_in_list = v;
    }

    /* Must be called after making new vcpu visible to for_each_vcpu(). */
    vcpu_check_shutdown( v );

    if ( !is_idle_domain( d ) )
        domain_update_node_affinity( d );

    return v;
}

static int late_hwdom_init( struct domain* d )
{
#ifdef CONFIG_LATE_HWDOM
    struct domain* dom0;
    int rv;

    if ( d != hardware_domain || d->domain_id == 0 )
        return 0;

    rv = xsm_init_hardware_domain( XSM_HOOK, d );
    if ( rv )
        return rv;

    printk( "Initialising hardware domain %d\n", hardware_domid );

    dom0 = rcu_lock_domain_by_id( 0 );
    ASSERT( dom0 != NULL );
    /*
     * Hardware resource ranges for domain 0 have been set up from
     * various sources intended to restrict the hardware domain's
     * access.  Apply these ranges to the actual hardware domain.
     *
     * Because the lists are being swapped, a side effect of this
     * operation is that Domain 0's rangesets are cleared.  Since
     * domain 0 should not be accessing the hardware when it constructs
     * a hardware domain, this should not be a problem.  Both lists
     * may be modified after this hypercall returns if a more complex
     * device model is desired.
     */
    rangeset_swap( d->irq_caps, dom0->irq_caps );
    rangeset_swap( d->iomem_caps, dom0->iomem_caps );
#ifdef CONFIG_X86
    rangeset_swap( d->arch.ioport_caps, dom0->arch.ioport_caps );
    setup_io_bitmap( d );
    setup_io_bitmap( dom0 );
#endif

    rcu_unlock_domain( dom0 );

    iommu_hwdom_init( d );

    return rv;
#else
    return 0;
#endif
}
}

#ifdef CONFIG_HAS_GDBSX
void domain_pause_for_debugger( void )
{
    struct vcpu* curr = current;
    struct domain* d = curr->domain;

    domain_pause_by_systemcontroller_nosync( d );

    /* if gdbsx active, we just need to pause the domain */
    if ( curr->arch.gdbsx_vcpu_event == 0 )
        send_global_virq( VIRQ_DEBUGGER );
}
#endif

grant_table_warn_active_grants( d );

for_each_vcpu( d, v )
{
    set_xen_guest_handle( runstate_guest( v ), NULL );
    unmap_vcpu_info( v );
}

rc = arch_domain_soft_reset( d );
if ( !rc )
    domain_resume( d );
else
    domain_crash( d );

return rc;
}

int vcpu_reset( struct vcpu* v )
{
    struct domain* d = v->domain;
    int rc;

    vcpu_pause( v );
    domain_lock( d );

    set_bit( _VPF_in_reset, &v->pause_flags );
    rc = arch_vcpu_reset( v );
    if ( rc )
        goto out_unlock;

    set_bit( _VPF_down, &v->pause_flags );

    clear_bit( v->vcpu_id, d->poll_mask );
    v->poll_evtchn = 0;

    v->fpu_initialised = 0;
    v->fpu_dirtied = 0;
    v->is_initialised = 0;
#ifdef VCPU_TRAP_LAST
    v->async_exception_mask = 0;
    memset( v->async_exception_state, 0, sizeof( v->async_exception_state ) );
#endif
    cpumask_clear( v->cpu_hard_affinity_tmp );
    clear_bit( _VPF_blocked, &v->pause_flags );
    clear_bit( _VPF_in_reset, &v->pause_flags );

out_unlock:
    domain_unlock( v->domain );
    vcpu_unpause( v );

    return rc;
}

/*
 * Map a guest page in and point the vcpu_info pointer at it.  This
 * makes sure that the vcpu_info is always pointing at a valid piece
 * of memory, and it sets a pending event to make sure that a pending
 * event doesn't get missed.
 */
int map_vcpu_info( struct vcpu* v, unsigned long gfn, unsigned offset )
{
    struct domain* d = v->domain;
    void* mapping;
    vcpu_info_t* new_info;
    struct page_info* page;
    int i;

    if ( offset > ( PAGE_SIZE - sizeof( vcpu_info_t ) ) )
        return -EINVAL;

    if ( v->vcpu_info_mfn != mfn_x( INVALID_MFN ) )
        return -EINVAL;

    /* Run this command on yourself or on other offline VCPUS. */
    if ( ( v != current ) && !( v->pause_flags & VPF_down ) )
        return -EINVAL;

    page = get_page_from_gfn( d, gfn, NULL, P2M_ALLOC );
    if ( !page )
        return -EINVAL;

    if ( !get_page_type( page, PGT_writable_page ) )
    {
        put_page( page );
        return -EINVAL;
    }

    mapping = __map_domain_page_global( page );
    if ( mapping == NULL )
    {
        put_page_and_type( page );
        return -ENOMEM;
    }

    new_info = ( vcpu_info_t* )( mapping + offset );

    if ( v->vcpu_info == &dummy_vcpu_info )
    {
        memset( new_info, 0, sizeof( *new_info ) );
#ifdef XEN_HAVE_PV_UPCALL_MASK
        __vcpu_info( v, new_info, evtchn_upcall_mask ) = 1;
#endif
    }
    else
    {
        memcpy( new_info, v->vcpu_info, sizeof( *new_info ) );
    }

    v->vcpu_info = new_info;
    v->vcpu_info_mfn = page_to_mfn( page );

    /* Set new vcpu_info pointer /before/ setting pending flags. */
    smp_wmb();

    /*
     * Mark everything as being pending just to make sure nothing gets
     * lost.  The domain will get a spurious event, but it can cope.
     */
    vcpu_info( v, evtchn_upcall_pending ) = 1;
    for ( i = 0; i < BITS_PER_EVTCHN_WORD( d ); i++ )
        set_bit( i, &vcpu_info( v, evtchn_pending_sel ) );
    arch_evtchn_inject( v );

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
