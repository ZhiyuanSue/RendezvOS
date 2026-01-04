#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/spmalloc.h>
#include <rendezvos/error.h>
DEFINE_PER_CPU(struct allocator*, kallocator);
struct mem_allocator tmp_sp_alloctor = {
        .init = sp_init,
        .m_alloc = sp_alloc,
        .m_free = sp_free,
};
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
        if (((vaddr)chunk) % PAGE_SIZE != 0 || chunk_order < 0
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
                padding_start += slot_size[chunk->chunk_order]
                                 + sizeof(struct object_header);
        }
        return 0;
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
                pr_error("[ERROR]illegal chunk free obj parameter obj %x chunk %x\n",obj,chunk);
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

        let's remember one thing: spmalloc is only used for kernel!!!!!!
        */
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]illegal chunk magic\n");
                return -E_IN_PARAM;
        }
        list_del(&obj->obj_list);
        list_add_head(&obj->obj_list, &chunk->empty_obj_list);
        /*we do not clean it*/
        chunk->nr_used_objs--;
        return obj->allocator_id;
}
static void*
collect_chunk_from_other_group(struct mem_allocator* sp_allocator_p,
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
                    && sp_allocator_p->groups[group_id].free_chunk_num) {
                        struct list_entry* tmp_list_entry =
                                sp_allocator_p->groups[group_id].empty_list.next;
                        while (tmp_list_entry
                               != &sp_allocator_p->groups[group_id].empty_list) {
                                struct mem_chunk* tmp_chunk =
                                        container_of(tmp_list_entry,
                                                     struct mem_chunk,
                                                     chunk_list);
                                if (tmp_chunk->nr_used_objs == 0) {
                                        alloc_chunk = tmp_chunk;
                                        list_del(&tmp_chunk->chunk_list);
                                        sp_allocator_p->groups[group_id]
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
struct allocator* sp_init(struct nexus_node* nexus_root, int allocator_id)
{
        if (!nexus_root || allocator_id < 0) {
                pr_error("[ERROR]illegal sp init parameter\n");
                return NULL;
        }
        tmp_sp_alloctor.allocator_id = allocator_id;
        tmp_sp_alloctor.nexus_root = nexus_root;
        tmp_sp_alloctor.page_chunk_root.rb_root = NULL;
        for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                tmp_sp_alloctor.groups[i].allocator_id = allocator_id;
                tmp_sp_alloctor.groups[i].chunk_order = i;
                tmp_sp_alloctor.groups[i].full_chunk_num = 0;
                tmp_sp_alloctor.groups[i].empty_chunk_num = 0;
                tmp_sp_alloctor.groups[i].free_chunk_num = 0;
                INIT_LIST_HEAD(&tmp_sp_alloctor.groups[i].empty_list);
                INIT_LIST_HEAD(&tmp_sp_alloctor.groups[i].full_list);
        }
        /*bootstrap*/
        struct mem_allocator* sp_allocator =
                sp_alloc((struct allocator*)&tmp_sp_alloctor,
                         sizeof(struct mem_allocator));
        if (sp_allocator) {
                memcpy(sp_allocator,
                       &tmp_sp_alloctor,
                       sizeof(struct mem_allocator));
                for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                        if (!list_empty(&sp_allocator->groups[i].empty_list)) {
                                list_replace(
                                        &tmp_sp_alloctor.groups[i].empty_list,
                                        &sp_allocator->groups[i].empty_list);
                        } else {
                                INIT_LIST_HEAD(
                                        &sp_allocator->groups[i].empty_list);
                                INIT_LIST_HEAD(
                                        &sp_allocator->groups[i].full_list);
                        }
                }
                if (per_cpu(kallocator, allocator_id)) {
                        pr_error(
                                "[ERROR]we have already have one allocator with id %d\n",
                                allocator_id);
                        return NULL;
                }
                per_cpu(kallocator, allocator_id) =
                        (struct allocator*)sp_allocator;
                ms_queue_node_t* buffer_idle_node =
                        sp_alloc((struct allocator*)&tmp_sp_alloctor,
                                 sizeof(ms_queue_node_t));
                msq_init(&sp_allocator->buffer_msq, buffer_idle_node);
                return (struct allocator*)sp_allocator;
        } else {
                pr_error(
                        "[ERROR]sp alloc cannot get a space of mem allocator\n");
                return NULL;
        }
}
static void* _sp_alloc(struct mem_allocator* sp_allocator_p, size_t Bytes)
{
        /*we allocate from the sp malloc*/
        int slot_index = bytes_to_slot(Bytes);
        struct mem_group* group = &sp_allocator_p->groups[slot_index];
        struct mem_chunk* alloc_chunk = NULL;
        if (!group->empty_chunk_num && !group->free_chunk_num) {
                alloc_chunk = collect_chunk_from_other_group(sp_allocator_p,
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
                                              sp_allocator_p->nexus_root,
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
                                /*just clean this time
                                 * allocated*/
                                free_pages(page_ptr,
                                           PAGE_PER_CHUNK,
                                           0,
                                           sp_allocator_p->nexus_root);
                                pr_error(
                                        "[ERROR]unknown reason lead to the chunk init fail\n");
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
                                &sp_allocator_p->groups[slot_index].empty_list,
                                &alloc_chunk->chunk_list);
                        group->empty_chunk_num++;
                }
        } else {
                alloc_chunk = container_of(
                        sp_allocator_p->groups[slot_index].empty_list.next,
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
static error_t _sp_free(void* p)
{
        struct mem_allocator* sp_allocator_p =
                (struct mem_allocator*)percpu(kallocator);
        /*
          even the p is invalid ,
          we still need to free the free requests on buffer list
        */

        /*the free requests should be fifo, so we free p at last*/
        if (!sp_allocator_p || !p) {
                pr_error("[ERROR] sp free with error parameter\n");
                return -E_IN_PARAM;
        }
        struct object_header* header =
                container_of(p, struct object_header, obj);
        struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                ((vaddr)p), PAGE_SIZE * PAGE_PER_CHUNK);
        if (header->allocator_id == sp_allocator_p->allocator_id) {
                bool full = (chunk->nr_max_objs == chunk->nr_used_objs);
                chunk_free_obj(header, chunk);
                int order = chunk->chunk_order;
                struct mem_group* group = &sp_allocator_p->groups[order];
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
                                free_pages(free_chunk,
                                           PAGE_PER_CHUNK,
                                           0,
                                           sp_allocator_p->nexus_root);
                        }
                        free_chunk = next_free_chunk;
                        if (&free_chunk->chunk_list == &group->empty_list)
                                break;
                }
        } else {
                /*put the free object to the target allocator lockfree buffer
                 * list*/
                msq_enqueue(&((struct mem_allocator*)per_cpu(
                                      kallocator, header->allocator_id))
                                     ->buffer_msq,
                            &header->msq_node);
        }
        return 0;
}
static void clean_buffer_msq(struct mem_allocator* sp_allocator_p)
{
        if (!sp_allocator_p)
                return;
        tagged_ptr_t dequeue_ptr = tp_new_none();
        while ((dequeue_ptr = msq_dequeue(&sp_allocator_p->buffer_msq)) != 0) {
                // ms_queue_node_t* ms_node =
                //         (ms_queue_node_t*)tp_get_ptr(dequeue_ptr);
                // struct object_header* header_ptr =
                //         container_of(tp_get_ptr(ms_node->next),
                //                      struct object_header,
                //                      msq_node);
                struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                        ((vaddr)dequeue_ptr), PAGE_SIZE * PAGE_PER_CHUNK);
                if(have_mapped(sp_allocator_p->nexus_root->vs,(vaddr)chunk,sp_allocator_p->nexus_root->handler)==0){
                        pr_error("the vpn is not mapped\n");
                }
                // bool full = (chunk->nr_max_objs == chunk->nr_used_objs);
                chunk_free_obj(container_of(tp_get_ptr(dequeue_ptr),
                                            struct object_header,
                                            msq_node),
                               chunk);
                // int order = chunk->chunk_order;
                // struct mem_group* group = &sp_allocator_p->groups[order];
                // if (full) {
                //         list_del(&chunk->chunk_list);
                //         list_add_tail(&chunk->chunk_list, &group->empty_list);
                //         group->empty_chunk_num++;
                //         group->full_chunk_num--;
                // } else if (chunk->nr_used_objs == 0) {
                //         group->free_chunk_num++;
                //         group->empty_chunk_num--;
                //         list_del(&chunk->chunk_list);
                //         list_add_head(&chunk->chunk_list, &group->empty_list);
                // }
        }
}
void* sp_alloc(struct allocator* allocator_p, size_t Bytes)
{
        if (!allocator_p) {
                pr_error("[ERROR]invalid allocator_p parameter\n");
                return NULL;
        }
        if (Bytes <= 0 || Bytes > MIDDLE_PAGE_SIZE) {
                pr_error("[ERROR]want to alloc bytes <= 0 or larger then 2M\n");
                return NULL;
        }
        struct mem_allocator* sp_allocator_p =
                (struct mem_allocator*)allocator_p;
        void* res_ptr = NULL;
        if (Bytes > slot_size[MAX_GROUP_SLOTS - 1]) {
                /*here we allocate from the page allocator*/
                int page_num = bytes_to_pages(Bytes);

                /*
                 we must try to allocate the page chunk node first,
                 then try to alloc the page
                */
                struct page_chunk_node* pcn =
                        (struct page_chunk_node*)_sp_alloc(
                                sp_allocator_p, sizeof(struct page_chunk_node));
                if (!pcn) {
                        pr_error("[ERROR]cannot allocate a page chunk node\n");
                        return res_ptr;
                }
                memset(pcn, 0, sizeof(struct page_chunk_node));
                res_ptr = get_free_page(page_num,
                                        KERNEL_VIRT_OFFSET,
                                        sp_allocator_p->nexus_root,
                                        0,
                                        PAGE_ENTRY_NONE);
                if (!res_ptr) {
                        pr_error("[ERROR] get free page fail\n");
                        return res_ptr;
                }
                pcn->page_addr = (vaddr)res_ptr;
                pcn->page_num = page_num;
                lock_cas(&sp_allocator_p->lock);
                page_chunk_rb_tree_insert(pcn,
                                          &sp_allocator_p->page_chunk_root);
                unlock_cas(&sp_allocator_p->lock);
        } else {
                res_ptr = _sp_alloc(sp_allocator_p, Bytes);
        }
        return res_ptr;
}
void sp_free(struct allocator* allocator_p, void* p)
{
        if (!allocator_p || !p) {
                pr_error("[ERROR] sp free error parameter\n");
                return;
        }
        struct mem_allocator* sp_allocator_p =
                (struct mem_allocator*)allocator_p;
        error_t e = 0;
        if (((vaddr)p) & (PAGE_SIZE - 1)) {
                clean_buffer_msq(sp_allocator_p);
                /*free from the chunks*/
                e = _sp_free(p);
        } else {
                /*free pages*/
                lock_cas(&sp_allocator_p->lock);
                struct page_chunk_node* pcn = page_chunk_rb_tree_search(
                        &sp_allocator_p->page_chunk_root, (vaddr)p);
                unlock_cas(&sp_allocator_p->lock);
                if (!pcn) {
                        /*another sp allocator alloced it*/
                        pr_error(
                                "cannot find the page chunk, might alloced by another sp allocator\n");
                        return;
                }
                e = free_pages(p, pcn->page_num, 0, sp_allocator_p->nexus_root);
                lock_cas(&sp_allocator_p->lock);
                page_chunk_rb_tree_remove(pcn,
                                          &sp_allocator_p->page_chunk_root);
                unlock_cas(&sp_allocator_p->lock);
                e = _sp_free((void*)pcn);
        }
        if (e) {
                pr_error(
                        "[ ERROR ]sp free have generated an error but no error handle\n");
        }
}