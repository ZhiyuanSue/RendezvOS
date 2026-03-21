#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/error.h>

/* Payload after object_header.obj for cross-CPU page-free MSQ nodes. */
struct kfree_page_free_info {
        vaddr page_vaddr;
        u32 src_cpu;
};

DEFINE_PER_CPU(struct allocator*, kallocator);
size_t slot_size[MAX_GROUP_SLOTS] =
        {8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048};
static int bytes_to_slot(size_t Bytes)
{
        /*for 0-2048 bytes, we calculate the slot index*/
        int slot_index = 0;
        for (; slot_index < MAX_GROUP_SLOTS; slot_index++) {
                if (slot_index == MAX_GROUP_SLOTS - 1
                    || slot_size[slot_index] >= Bytes)
                        break;
        }
        return slot_index;
}
static int bytes_to_pages(size_t Bytes)
{
        /*for 2048 - 2M, alloc pages ,checked by upper logical*/
        return 1 << log2_of_next_power_of_two(ROUND_UP(Bytes, PAGE_SIZE)
                                              / PAGE_SIZE);
}

static void page_chunk_rb_tree_insert(struct page_chunk_node* node,
                                      struct rb_root* page_chunk_root)
{
        struct rb_node **new = &page_chunk_root->rb_root, *parent = NULL;
        u64 key = node->page_addr;
        while (*new) {
                parent = *new;
                struct page_chunk_node* tmp_node =
                        container_of(parent, struct page_chunk_node, _rb_node);
                if (key < (u64)(tmp_node->page_addr))
                        new = &parent->left_child;
                else if (key > (u64)(tmp_node->page_addr))
                        new = &parent->right_child;
                else
                        return;
        }
        RB_Link_Node(&node->_rb_node, parent, new);
        RB_SolveDoubleRed(&node->_rb_node, page_chunk_root);
}
static void page_chunk_rb_tree_remove(struct page_chunk_node* node,
                                      struct rb_root* page_chunk_root)
{
        RB_Remove(&node->_rb_node, page_chunk_root);
        node->_rb_node.black_height = node->_rb_node.rb_parent_color = 0;
        node->_rb_node.left_child = node->_rb_node.right_child = NULL;
}
struct page_chunk_node*
page_chunk_rb_tree_search(struct rb_root* page_chunk_root, vaddr page_addr)
{
        struct rb_node* node = page_chunk_root->rb_root;
        struct page_chunk_node* tmp_node = NULL;
        while (node) {
                tmp_node = container_of(node, struct page_chunk_node, _rb_node);
                if (page_addr < tmp_node->page_addr)
                        node = node->left_child;
                else if (page_addr > (u64)(tmp_node->page_addr))
                        node = node->right_child;
                else
                        return tmp_node;
        }
        return NULL;
}

error_t chunk_init(struct mem_chunk* chunk, int chunk_order, int allocator_id)
{
        if (!chunk || ((vaddr)chunk) % PAGE_SIZE != 0 || chunk_order < 0
            || chunk_order >= MAX_GROUP_SLOTS) {
                pr_info("the chunk init parameter is wrong, please check\n");
                return -E_IN_PARAM;
        }
        chunk->magic = CHUNK_MAGIC;
        chunk->chunk_order = chunk_order;
        chunk->allocator_id = allocator_id;
        INIT_LIST_HEAD(&chunk->chunk_list);
        INIT_LIST_HEAD(&chunk->full_obj_list);
        INIT_LIST_HEAD(&chunk->empty_obj_list);
        size_t obj_size = sizeof(struct object_header) + slot_size[chunk_order];
        chunk->nr_used_objs = 0;
        int obj_num = (PAGE_SIZE * PAGE_PER_CHUNK - sizeof(struct mem_chunk))
                      / obj_size;
        chunk->nr_max_objs = obj_num;
        int padding_start = sizeof(struct mem_chunk);
        for (int i = 0; i < obj_num; i++) {
                /* add the following check to test whether the obj start is 4k
                 * aligned*/
                // if((padding_start+sizeof(struct object_header))%4096==0){
                //         pr_error("error padding order
                //         %d\n",chunk->chunk_order);
                // }
                struct object_header* obj_ptr =
                        (struct object_header*)((vaddr)chunk + padding_start);
                list_add_head(&obj_ptr->obj_list, &chunk->empty_obj_list);
                obj_ptr->allocator_id = allocator_id;
                memset(&obj_ptr->msq_node, 0, sizeof(ms_queue_node_t));
                padding_start += slot_size[chunk->chunk_order]
                                 + sizeof(struct object_header);
        }
        return REND_SUCCESS;
}
struct object_header* chunk_get_obj(struct mem_chunk* chunk)
{
        if (!chunk) {
                pr_error(
                        "[ERROR]the chunk get obj input parameter is wrong please check\n");
                return NULL;
        }
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]bad chunk magic %x, please check\n",
                         chunk->magic);
                return NULL;
        }
        if (chunk->nr_used_objs >= chunk->nr_max_objs) {
                /*
                        means no mem can use in this chunk
                        please check the return value
                */
                pr_error(
                        "[ERROR] chunk's used obj is %d, larger or equal then max obj number\n",
                        chunk->nr_used_objs);
                return NULL;
        }
        struct object_header* get_obj =
                (struct object_header*)(chunk->empty_obj_list.next);
        list_del(&get_obj->obj_list);
        list_add_head(&get_obj->obj_list, &chunk->full_obj_list);
        chunk->nr_used_objs++;
        return get_obj;
}
error_t chunk_free_obj(struct object_header* obj, struct mem_chunk* chunk)
{
        /*allocator id must exist*/
        if (!obj || !chunk) {
                pr_error(
                        "[ERROR]illegal chunk free obj parameter obj %x chunk %x\n",
                        obj,
                        chunk);
                return -E_IN_PARAM;
        }
        /*
        the legal obj must not be 4K aligned, but we do not check here
        if it's a 4K aligned, which means it is a page alloc, and should use the
        free pages
        but there's a problem that how can we know the pages we need to free?
        let's check the nexus realization.
        in nexus, for kernel space, one nexus entry record the page num.
        because it's ppn is continuous.
        but for user space, the ppn might not.
        so we have to let every entry record 4K or 2M.
        and here we must need the page_num to decide how much to return

        let's remember one thing: kmalloc is only used for kernel!!!!!!
        */
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]illegal chunk magic\n");
                return -E_IN_PARAM;
        }
        list_del(&obj->obj_list);
        list_add_head(&obj->obj_list, &chunk->empty_obj_list);
        /*we do not clean it*/
        chunk->nr_used_objs--;
        memset(&obj->msq_node, 0, sizeof(ms_queue_node_t));
        return obj->allocator_id;
}
error_t group_free_obj(struct object_header* header, struct mem_chunk* chunk,
                       struct mem_group* group)
{
        if (!header || !chunk || !group)
                return -E_IN_PARAM;
        error_t res = 0;
        bool full = (chunk->nr_max_objs == chunk->nr_used_objs);
        if ((res = chunk_free_obj(header, chunk)) < 0)
                return res;
        if (full) {
                list_del(&chunk->chunk_list);
                list_add_tail(&chunk->chunk_list, &group->empty_list);
                group->empty_chunk_num++;
                group->full_chunk_num--;
        } else if (chunk->nr_used_objs == 0) {
                group->free_chunk_num++;
                group->empty_chunk_num--;
                list_del(&chunk->chunk_list);
                list_add_head(&chunk->chunk_list, &group->empty_list);
        }
        return res;
}

/* Free function for buffer_msq nodes (both dummy and data nodes).
 * Returns the object_header to the memory pool via group_free_obj. */
static void free_buffer_object(ref_count_t* refcount)
{
        ms_queue_node_t* node =
                container_of(refcount, ms_queue_node_t, refcount);
        struct object_header* header = container_of(
                (ms_queue_node_t*)node, struct object_header, msq_node);
        struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                ((vaddr)header), PAGE_SIZE * PAGE_PER_CHUNK);
        int order = chunk->chunk_order;
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)percpu(kallocator);
        struct mem_group* group = &k_allocator_p->groups[order];
        group_free_obj(header, chunk, group);
}
static void* collect_chunk_from_other_group(struct mem_allocator* k_allocator_p,
                                            int slot_index)
{
        struct mem_chunk* alloc_chunk = NULL;
        /*
                if we have no space to alloc
                we first try to find other groups for help
        */
        for (int group_id = 0; group_id < MAX_GROUP_SLOTS; group_id++) {
                /*skip current group*/
                if (group_id != slot_index
                    && k_allocator_p->groups[group_id].free_chunk_num) {
                        struct list_entry* tmp_list_entry =
                                k_allocator_p->groups[group_id].empty_list.next;
                        while (tmp_list_entry
                               != &k_allocator_p->groups[group_id].empty_list) {
                                struct mem_chunk* tmp_chunk =
                                        container_of(tmp_list_entry,
                                                     struct mem_chunk,
                                                     chunk_list);
                                if (tmp_chunk->nr_used_objs == 0) {
                                        alloc_chunk = tmp_chunk;
                                        list_del(&tmp_chunk->chunk_list);
                                        k_allocator_p->groups[group_id]
                                                .free_chunk_num--;
                                        break;
                                }
                                tmp_list_entry = tmp_list_entry->next;
                        }
                }
                if (alloc_chunk)
                        break;
        }
        return alloc_chunk;
}

static void* _k_alloc(struct mem_allocator* k_allocator_p, size_t Bytes)
{
        /*we allocate from the  kmalloc*/
        int slot_index = bytes_to_slot(Bytes);
        struct mem_group* group = &k_allocator_p->groups[slot_index];
        struct mem_chunk* alloc_chunk = NULL;
        if (!group->empty_chunk_num && !group->free_chunk_num) {
                alloc_chunk = collect_chunk_from_other_group(k_allocator_p,
                                                             slot_index);
                if (!alloc_chunk) {
                        /*
                                other group also have no chunk
                                get some pages from the chunk
                        */
                        /* here we will change the group linked list, we
                         * also need to think the lock*/
                        void* page_ptr =
                                get_free_page(PAGE_PER_CHUNK,
                                              KERNEL_VIRT_OFFSET,
                                              k_allocator_p->nexus_root,
                                              0,
                                              PAGE_ENTRY_NONE);
                        if (!page_ptr) {
                                pr_error("[ERROR] get free page fail\n");
                                return NULL;
                        }
                        error_t e = chunk_init((struct mem_chunk*)page_ptr,
                                               group->chunk_order,
                                               group->allocator_id);
                        if (e) {
                                pr_error(
                                        "[ERROR]unknown reason lead to the chunk init fail e code %d\n",
                                        e);
                                /*just clean this time
                                 * allocated*/
                                e = free_pages(page_ptr,
                                               PAGE_PER_CHUNK,
                                               0,
                                               k_allocator_p->nexus_root);
                                if (e) {
                                        pr_error(
                                                "[Error] fail to free pages e code %d\n",
                                                e);
                                }
                                return NULL;
                        }
                        list_add_head(
                                &((struct mem_chunk*)page_ptr)->chunk_list,
                                &group->empty_list);
                        /*get one alloc chunk*/
                        alloc_chunk = page_ptr;
                        group->empty_chunk_num++;
                } else {
                        /*we get another group's chunk,
                        we need to re-init it*/
                        error_t e = chunk_init(alloc_chunk,
                                               group->chunk_order,
                                               group->allocator_id);
                        if (e) {
                                pr_error("[ERROR]re init the chunk fail\n");
                                return NULL;
                        }
                        list_add_head(
                                &k_allocator_p->groups[slot_index].empty_list,
                                &alloc_chunk->chunk_list);
                        group->empty_chunk_num++;
                }
        } else {
                alloc_chunk = container_of(
                        k_allocator_p->groups[slot_index].empty_list.next,
                        struct mem_chunk,
                        chunk_list);
                if (alloc_chunk->nr_used_objs == 0) {
                        group->free_chunk_num--;
                        group->empty_chunk_num++;
                }
        }
        struct object_header* obj_ptr = chunk_get_obj(alloc_chunk);
        if (!obj_ptr) {
                pr_error("[ERROR]cannot get a object from chunk \n");
                return NULL;
        }
        if (alloc_chunk->nr_max_objs == alloc_chunk->nr_used_objs) {
                /*all the obj in this chunk are used, move this chunk to full
                 * list*/
                list_del(&alloc_chunk->chunk_list);
                list_add_head(&alloc_chunk->chunk_list, &group->full_list);
                group->empty_chunk_num--;
                group->full_chunk_num++;
        }
        return obj_ptr->obj;
}
static error_t _k_free(void* p)
{
        error_t res = 0;
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)percpu(kallocator);
        /*
          even the p is invalid ,
          we still need to free the free requests on buffer list
        */

        /*the free requests should be fifo, so we free p at last*/
        if (!k_allocator_p || !p) {
                pr_error("[ERROR]  kfree with error parameter\n");
                return -E_IN_PARAM;
        }
        struct object_header* header =
                container_of(p, struct object_header, obj);
        struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                ((vaddr)p), PAGE_SIZE * PAGE_PER_CHUNK);
        if (header->allocator_id == k_allocator_p->allocator_id) {
                int order = chunk->chunk_order;
                struct mem_group* group = &k_allocator_p->groups[order];
                if ((res = group_free_obj(header, chunk, group)) < 0)
                        return res;

                /*
                        then we calculate the free chunk numbers of that group
                        if the free chunks num is tooo large ,we free some of
                   the chunks
                */
                struct mem_chunk* free_chunk = container_of(
                        group->empty_list.next, struct mem_chunk, chunk_list);
                struct mem_chunk* next_free_chunk = free_chunk;
                while (group->free_chunk_num > 1) {
                        next_free_chunk =
                                container_of(free_chunk->chunk_list.next,
                                             struct mem_chunk,
                                             chunk_list);
                        if (!(free_chunk->nr_used_objs)) {
                                list_del(&free_chunk->chunk_list);
                                group->free_chunk_num--;
                                res = free_pages(free_chunk,
                                                 PAGE_PER_CHUNK,
                                                 0,
                                                 k_allocator_p->nexus_root);
                                if (res != REND_SUCCESS) {
                                        pr_error(
                                                "[Error] k free's free page fail\n");
                                }
                        }
                        free_chunk = next_free_chunk;
                        if (&free_chunk->chunk_list == &group->empty_list)
                                break;
                }
        } else {
                /*put the free object to the target allocator lockfree buffer
                 * list*/
                struct mem_allocator* k_allocator_p =
                        (struct mem_allocator*)per_cpu(kallocator,
                                                       header->allocator_id);
                ref_init(&header->msq_node.refcount);
                msq_enqueue(k_allocator_p->buffer_msq,
                            &header->msq_node,
                            free_buffer_object);
                ref_put(&header->msq_node.refcount, free_buffer_object);
                header = NULL;
                atomic64_inc(&k_allocator_p->buffer_size);
        }
        return REND_SUCCESS;
}

static error_t kfree_page_local(struct mem_allocator* k_allocator_p, void* p)
{
        error_t e = REND_SUCCESS;
        if (!k_allocator_p || !p)
                return -E_IN_PARAM;
        lock_cas(&k_allocator_p->lock);
        struct page_chunk_node* pcn = page_chunk_rb_tree_search(
                &k_allocator_p->page_chunk_root, (vaddr)p);
        unlock_cas(&k_allocator_p->lock);
        if (!pcn) {
                pr_error(
                        "[ERROR] kfree_page_local: missing page_chunk for %x\n",
                        (vaddr)p);
                return -E_RENDEZVOS;
        }
        e = free_pages(p, pcn->page_num, 0, k_allocator_p->nexus_root);
        if (e) {
                pr_error("[ ERROR ] kfree_page_local free_pages failed\n");
                return e;
        }
        lock_cas(&k_allocator_p->lock);
        page_chunk_rb_tree_remove(pcn, &k_allocator_p->page_chunk_root);
        unlock_cas(&k_allocator_p->lock);
        if (_k_free((void*)pcn)) {
                pr_error("[ ERROR ] kfree_page_local _k_free pcn error\n");
                return e;
        }
        return REND_SUCCESS;
}

static void free_kpage_msg(ref_count_t* refcount)
{
        ms_queue_node_t* msq_node =
                container_of(refcount, ms_queue_node_t, refcount);
        struct object_header* header =
                container_of(msq_node, struct object_header, msq_node);
        struct kfree_page_free_info* free_info =
                (struct kfree_page_free_info*)header->obj;
        /*
         * Dummy head for kfree_page_msq uses page_vaddr == 0; recycle like
         * buffer_msq dummy (small-object pool).
         */
        if (free_info->page_vaddr == 0) {
                free_buffer_object(refcount);
                return;
        }
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)percpu(kallocator);
        if (!k_allocator_p) {
                pr_error("[ERROR] free_kpage_msg: no percpu kallocator\n");
                goto free_info;
        }
        error_t e = kfree_page_local(k_allocator_p,
                                     (void*)free_info->page_vaddr);
        if (e) {
                pr_error(
                        "[kfree_page_msq] free_kpage_msg: kfree_page_local failed e=%d page_vaddr=%x src_cpu=%u\n",
                        e,
                        (i64)free_info->page_vaddr,
                        (unsigned)free_info->src_cpu);
        }
free_info:
        struct allocator* src_cpu_kallocator =
                per_cpu(kallocator, (int)free_info->src_cpu);
        if (src_cpu_kallocator)
                src_cpu_kallocator->m_free(src_cpu_kallocator,
                                           (void*)header->obj);
}

/*
 * Both queues carry work from other CPUs onto this allocator: whole-page
 * kfree (kfree_page_msq) and small-object kfree (buffer_msq). Drain together
 * on every kalloc/kfree entry so page and object paths behave the same and
 * backlog stays low.
 */
static void mem_allocator_remote_frees(struct mem_allocator* k_allocator_p)
{
        tagged_ptr_t dequeue_tagged_ptr;
        if (k_allocator_p->kfree_page_msq) {
                while (atomic64_load((volatile u64*)&(
                               k_allocator_p->kfree_page_pending.counter))
                       && (dequeue_tagged_ptr =
                                   msq_dequeue(k_allocator_p->kfree_page_msq,
                                               free_kpage_msg))) {
                        ref_put(&((ms_queue_node_t*)tp_get_ptr(
                                          dequeue_tagged_ptr))
                                         ->refcount,
                                free_kpage_msg);
                        atomic64_dec(&k_allocator_p->kfree_page_pending);
                }
        }
        if (k_allocator_p->buffer_msq) {
                /* free_func releases old dummy (object_header), not the data
                 * node. */
                while (atomic64_load((volatile u64*)&(
                               k_allocator_p->buffer_size.counter))
                       && (dequeue_tagged_ptr =
                                   msq_dequeue(k_allocator_p->buffer_msq,
                                               free_buffer_object))) {
                        /* Release ref held by dequeue on the data node.
                         * When refcount drops to 0, free_buffer_object will
                         * automatically return it to the pool via
                         * group_free_obj. */
                        ref_put(&((ms_queue_node_t*)tp_get_ptr(
                                          dequeue_tagged_ptr))
                                         ->refcount,
                                free_buffer_object);
                        atomic64_dec(&k_allocator_p->buffer_size);
                }
        }
}

void kalloc_process_cross_cpu_frees(void)
{
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)percpu(kallocator);
        if (!k_allocator_p)
                return;
        mem_allocator_remote_frees(k_allocator_p);
}

static void* kalloc(struct allocator* allocator_p, size_t Bytes)
{
        if (!allocator_p) {
                pr_error("[ERROR]invalid allocator_p parameter\n");
                return NULL;
        }
        if (Bytes <= 0 || Bytes > MIDDLE_PAGE_SIZE) {
                pr_error("[ERROR]want to alloc bytes <= 0 or larger then 2M\n");
                return NULL;
        }
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)allocator_p;
        mem_allocator_remote_frees(k_allocator_p);
        void* res_ptr = NULL;
        if (Bytes > slot_size[MAX_GROUP_SLOTS - 1]) {
                /*here we allocate from the page allocator*/
                int page_num = bytes_to_pages(Bytes);

                /*
                 we must try to allocate the page chunk node first,
                 then try to alloc the page
                */
                struct page_chunk_node* pcn = (struct page_chunk_node*)_k_alloc(
                        k_allocator_p, sizeof(struct page_chunk_node));
                if (!pcn) {
                        pr_error("[ERROR]cannot allocate a page chunk node\n");
                        return res_ptr;
                }
                memset(pcn, 0, sizeof(struct page_chunk_node));
                res_ptr = get_free_page(page_num,
                                        KERNEL_VIRT_OFFSET,
                                        k_allocator_p->nexus_root,
                                        0,
                                        PAGE_ENTRY_NONE);
                if (!res_ptr) {
                        pr_error("[ERROR] get free page fail\n");
                        return res_ptr;
                }
                pcn->page_addr = (vaddr)res_ptr;
                pcn->page_num = page_num;
                lock_cas(&k_allocator_p->lock);
                page_chunk_rb_tree_insert(pcn, &k_allocator_p->page_chunk_root);
                unlock_cas(&k_allocator_p->lock);
        } else {
                res_ptr = _k_alloc(k_allocator_p, Bytes);
        }
        return res_ptr;
}
static void kfree(struct allocator* allocator_p, void* p)
{
        if (!allocator_p || !p) {
                pr_error("[ERROR] kfree error parameter\n");
                return;
        }
        struct mem_allocator* k_allocator_p =
                (struct mem_allocator*)allocator_p;
        error_t e = REND_SUCCESS;
        mem_allocator_remote_frees(k_allocator_p);
        if (((vaddr)p) & (PAGE_SIZE - 1)) {
                /*free from the chunks*/
                e = _k_free(p);
        } else {
                u32 cur_cpu = percpu(cpu_number);
                int owner_cpu = nexus_kernel_page_owner_cpu((vaddr)p);
                if (owner_cpu == INVALID_CPU_ID) {
                        e = _k_free(p);
                } else if ((int)cur_cpu != owner_cpu) {
                        struct mem_allocator* owner_mem_allocator =
                                (struct mem_allocator*)per_cpu(kallocator,
                                                               owner_cpu);
                        if (!owner_mem_allocator
                            || !owner_mem_allocator->kfree_page_msq) {
                                pr_error(
                                        "[ERROR] kfree page: target kallocator/msq missing cpu %d\n",
                                        owner_cpu);
                                return;
                        }
                        struct allocator* src_cpu_kallocator =
                                percpu(kallocator);
                        void* free_info = src_cpu_kallocator->m_alloc(
                                src_cpu_kallocator,
                                sizeof(struct kfree_page_free_info));
                        if (!free_info) {
                                pr_error(
                                        "[ERROR] kfree page: cannot alloc cross-cpu msg\n");
                                return;
                        }
                        memset(free_info,
                               0,
                               sizeof(struct kfree_page_free_info));
                        struct kfree_page_free_info* info =
                                (struct kfree_page_free_info*)free_info;
                        info->page_vaddr = (vaddr)p;
                        info->src_cpu = cur_cpu;
                        struct object_header* oh = container_of(
                                free_info, struct object_header, obj);
                        ref_init(&oh->msq_node.refcount);
                        msq_enqueue(owner_mem_allocator->kfree_page_msq,
                                    &oh->msq_node,
                                    free_kpage_msg);
                        ref_put(&oh->msq_node.refcount, free_kpage_msg);
                        atomic64_inc(&owner_mem_allocator->kfree_page_pending);
                        return;
                }
                e = kfree_page_local(k_allocator_p, p);
                if (e == -E_RENDEZVOS) {
                        e = _k_free(p);
                }
        }
        if (e) {
                pr_error(
                        "[ ERROR ]kfree have generated an error but no error handle\n");
        }
}
struct mem_allocator tmp_k_alloctor = {
        .init = kinit,
        .m_alloc = kalloc,
        .m_free = kfree,
        .buffer_msq = NULL,
        .kfree_page_msq = NULL,
};
struct allocator* kinit(struct nexus_node* nexus_root, int allocator_id)
{
        if (!nexus_root || allocator_id < 0) {
                pr_error("[ERROR]illegal sp init parameter\n");
                return NULL;
        }
        tmp_k_alloctor.allocator_id = allocator_id;
        tmp_k_alloctor.nexus_root = nexus_root;
        tmp_k_alloctor.page_chunk_root.rb_root = NULL;
        for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                tmp_k_alloctor.groups[i].allocator_id = allocator_id;
                tmp_k_alloctor.groups[i].chunk_order = i;
                tmp_k_alloctor.groups[i].full_chunk_num = 0;
                tmp_k_alloctor.groups[i].empty_chunk_num = 0;
                tmp_k_alloctor.groups[i].free_chunk_num = 0;
                INIT_LIST_HEAD(&tmp_k_alloctor.groups[i].empty_list);
                INIT_LIST_HEAD(&tmp_k_alloctor.groups[i].full_list);
        }
        /*bootstrap*/
        struct mem_allocator* k_allocator =
                kalloc((struct allocator*)&tmp_k_alloctor,
                       sizeof(struct mem_allocator));
        if (k_allocator) {
                memcpy(k_allocator,
                       &tmp_k_alloctor,
                       sizeof(struct mem_allocator));
                for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                        if (!list_empty(&k_allocator->groups[i].empty_list)) {
                                list_replace(
                                        &tmp_k_alloctor.groups[i].empty_list,
                                        &k_allocator->groups[i].empty_list);
                        } else {
                                INIT_LIST_HEAD(
                                        &k_allocator->groups[i].empty_list);
                                INIT_LIST_HEAD(
                                        &k_allocator->groups[i].full_list);
                        }
                }
                if (per_cpu(kallocator, allocator_id)) {
                        pr_error(
                                "[ERROR]we have already have one allocator with id %d\n",
                                allocator_id);
                        return NULL;
                }
                per_cpu(kallocator, allocator_id) =
                        (struct allocator*)k_allocator;
                void* buffer_idle_node = kalloc(
                        (struct allocator*)&tmp_k_alloctor, slot_size[0]);
                struct object_header* idle_obj_ptr = container_of(
                        buffer_idle_node, struct object_header, obj);
                ms_queue_t* buffer_msq = kalloc(
                        (struct allocator*)&tmp_k_alloctor, sizeof(ms_queue_t));
                msq_init(buffer_msq, &idle_obj_ptr->msq_node, 0);
                atomic64_init(&k_allocator->buffer_size, 0);
                idle_obj_ptr->allocator_id = allocator_id;
                k_allocator->buffer_msq = buffer_msq;

                void* kernel_free_page_info_idle_node =
                        kalloc((struct allocator*)&tmp_k_alloctor,
                               sizeof(struct kfree_page_free_info));
                if (!kernel_free_page_info_idle_node) {
                        pr_error("[ERROR] kfree_page_msq idle alloc fail\n");
                        return NULL;
                }
                memset(kernel_free_page_info_idle_node,
                       0,
                       sizeof(struct kfree_page_free_info));
                struct object_header* kernel_free_page_idle_header =
                        container_of(kernel_free_page_info_idle_node,
                                     struct object_header,
                                     obj);
                ms_queue_t* kernel_free_page_msq = kalloc(
                        (struct allocator*)&tmp_k_alloctor, sizeof(ms_queue_t));
                if (!kernel_free_page_msq) {
                        pr_error("[ERROR] kfree_page_msq struct alloc fail\n");
                        return NULL;
                }
                msq_init(kernel_free_page_msq,
                         &kernel_free_page_idle_header->msq_node,
                         0);
                kernel_free_page_idle_header->allocator_id = allocator_id;
                atomic64_init(&k_allocator->kfree_page_pending, 0);
                k_allocator->kfree_page_msq = kernel_free_page_msq;

                return (struct allocator*)k_allocator;
        } else {
                pr_error("[ERROR]kalloc cannot get a space of mem allocator\n");
                return NULL;
        }
}