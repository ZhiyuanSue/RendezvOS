# IPC — API reference (ports and messages)

> **文档角色：** 对外 API / 调用契约（给实现 compat、server、或修改 IPC 调用点的读者与 AI）  
> **设计原理与无锁细节：** 仅见 [`lockfree-ipc.md`](lockfree-ipc.md) — 不在此重复 MS 队列推导  
> **源码真源：** `core/kernel/ipc/ipc.c` · 头文件 `core/include/rendezvos/ipc/ipc.h`  
> **上层用法入口：** [`USING_CORE.md`](USING_CORE.md) §3.1

---

## 1. Scope

| In scope | Out of scope (see elsewhere) |
|----------|------------------------------|
| Public functions in `rendezvos/ipc/ipc.h` | MS-queue / EBR / tagged-ptr algorithms → `lockfree-ipc.md` |
| Caller obligations (enqueue / send / recv / dequeue) | Why lock-free IPC was chosen → `lockfree-ipc.md` §1 |
| Return codes as implemented in `ipc.c` | `cancel_ipc` — **不存在**；替代方案 → §7 |
| Push vs pull (who calls `ipc_transfer_message`) | kmsg TLV 编码细节 → `ipc/kmsg.h`, `ipc_serial.h` |
| System async outbound (`ipc_system_*`) | MS / persona derivation → `lockfree-ipc.md` §9 |

**Do not** refactor `send_msg` / `recv_msg` together with try variants: try APIs are a **copied** match+transfer branch only (`ipc.c` comments).

---

## 2. Architecture — two layers

IPC = **port rendezvous** (threads) + **per-thread message queues** (payloads). Do not pass `Message_t*` into `send_msg` / `ipc_try_send_msg`.

```mermaid
flowchart TB
  subgraph port_layer [Layer A — Port rendezvous]
    PQ["Message_Port_t::thread_queue"]
    IR["Ipc_Request_t nodes"]
    PQ --- IR
  end

  subgraph msg_layer [Layer B — Per-thread messages]
    SQ["Thread_Base::send_msg_queue"]
    RQ["Thread_Base::recv_msg_queue"]
    MT["Message_t + Msg_Data_t"]
    SQ --- MT
    RQ --- MT
  end

  API_PORT["send_msg / recv_msg / ipc_try_*"]
  API_MSG["enqueue_msg_for_send / dequeue_recv_msg"]
  XFER["ipc_transfer_message (internal)"]

  API_PORT --> port_layer
  API_MSG --> msg_layer
  API_PORT --> XFER
  XFER --> SQ
  XFER --> RQ
```

| Layer | Queue location | Element type | Public API |
|-------|----------------|--------------|------------|
| A | `port->thread_queue` | `Ipc_Request_t` | `send_msg`, `recv_msg`, `ipc_try_send_msg`, `ipc_try_recv_msg` |
| B | `thread->send_msg_queue` / `recv_msg_queue` | `Message_t` | `enqueue_msg_for_send`, `dequeue_recv_msg` |

**Invariant:** Port queue holds **waiters**, not messages. Port queue is **single-state** (all SEND or all RECV waiters). See `lockfree-ipc.md` §6.

**Transfer:** `ipc_transfer_message` dequeues one message from sender’s send queue (or `send_pending_msg`), copies data via `fill_message_data`, enqueues a **new** `Message_t` on receiver’s recv queue (`lockfree-ipc.md` §3.3).

---

## 3. Push vs pull

After rendezvous, **the thread that did not block on the port** runs `ipc_transfer_message`.

```mermaid
sequenceDiagram
  participant S as Sender thread
  participant SQ as Sender send_msg_queue
  participant P as Port thread_queue
  participant R as Receiver thread
  participant RQ as Receiver recv_msg_queue

  Note over R,P: Receiver already blocked (recv_msg)
  S->>SQ: enqueue_msg_for_send(msg)
  S->>P: send_msg / ipc_try_send_msg (try_match SEND)
  S->>S: ipc_transfer_message(S, R)
  S->>SQ: dequeue one Message_t
  S->>RQ: enqueue copy
  S->>R: status block_on_receive → ready
  R->>RQ: dequeue_recv_msg()
```

```mermaid
sequenceDiagram
  participant S as Sender thread
  participant SQ as Sender send_msg_queue
  participant P as Port thread_queue
  participant R as Receiver thread
  participant RQ as Receiver recv_msg_queue

  Note over S,P: Sender already blocked (send_msg after enqueue)
  S->>SQ: enqueue_msg_for_send(msg) before block
  S->>P: blocked on port (SEND waiter)
  R->>P: recv_msg / ipc_try_recv_msg (try_match RECV)
  R->>R: ipc_transfer_message(S, R)
  R->>SQ: dequeue from sender queue
  R->>RQ: enqueue copy
  R->>S: status block_on_send → ready
  R->>RQ: dequeue_recv_msg()
```

---

## 4. Blocking vs non-blocking

Same message-queue rules; difference is **only** when `ipc_port_try_match` returns NULL.

```mermaid
flowchart TD
  START([send_msg or ipc_try_send_msg])
  ENQ[Caller already called enqueue_msg_for_send]
  MATCH{ipc_port_try_match}
  XFER[ipc_transfer_message]
  OK{transfer result}
  BLOCK[ipc_port_enqueue_wait + schedule]
  AGAIN_RET[return -E_REND_AGAIN]
  SUCC[return REND_SUCCESS]

  START --> ENQ --> MATCH
  MATCH -->|matched| XFER --> OK
  OK -->|REND_SUCCESS| SUCC
  OK -->|-E_REND_AGAIN| MATCH
  OK -->|other| ERR[return error]
  MATCH -->|NULL| BLK{which API?}
  BLK -->|send_msg| BLOCK --> SUCC
  BLK -->|ipc_try_send_msg| AGAIN_RET
```

| | `send_msg` / `recv_msg` | `ipc_try_send_msg` / `ipc_try_recv_msg` |
|---|-------------------------|----------------------------------------|
| Before send | `enqueue_msg_for_send(msg)` | **Same** |
| After recv success | `dequeue_recv_msg()` | **Same** |
| No peer on port | Enqueue self on port + `schedule()` | Return `-E_REND_AGAIN` immediately |
| Implementation | Full `while` loop in `ipc.c` | **Copied** match+transfer loop only; **do not share helpers with blocking** |

---

## 5. Public API — quick reference

Headers: `rendezvos/ipc/ipc.h`, `ipc/port.h`, `ipc/message.h`, `ipc/kmsg.h`.

| Function | Preconditions | Postconditions (success) | Blocks |
|----------|---------------|--------------------------|--------|
| `enqueue_msg_for_send(msg)` | Current thread; valid msg refcount | `msg` on current send queue | No |
| `send_msg(port)` | Prior `enqueue_msg_for_send` | One msg transferred off send side; peer may be `ready` | If no peer |
| `ipc_try_send_msg(port)` | Prior `enqueue_msg_for_send` | Same transfer as send on match | No |
| `recv_msg(port)` | — | One msg on current recv queue | If no peer |
| `ipc_try_recv_msg(port)` | — | Same as recv on match | No |
| `dequeue_recv_msg()` | After recv API returned `REND_SUCCESS` | Returns `Message_t*` or NULL; decrements `recv_pending_cnt` | No |
| `ipc_system_try_deliver(port, msg, use_system_proxy)` | Valid `msg` refs; thread ctx if proxy false | Staged try_send to port waiter | No |
| `ipc_system_deliver_to(receiver, msg, use_system_proxy)` | Valid `msg` refs; bound `receiver` | Staged transfer to recv queue | No |

**Not exported:** `ipc_port_try_match`, `ipc_port_enqueue_wait` (internal to `ipc.c`).

**Core async only:** `ipc_system_*` — see §10. Do **not** use `enqueue_msg_for_send` + `ipc_try_send_msg` from IRQ with `get_cpu_current_thread()`.

---

## 6. Per-function behavior (matches `ipc.c`)

### 6.1 `send_msg(port)`

1. `ipc_port_try_match(port, IPC_PORT_STATE_SEND)` (reuse `receiver_request` on retry).
2. **Matched:** `ipc_transfer_message(sender, receiver)`.
   - `REND_SUCCESS` → receiver `block_on_receive` → `ready`; `ref_put(Ipc_Request)`; return.
   - `-E_REND_AGAIN` → `continue`.
   - Else → `ref_put`; return error.
3. **Not matched:** status → `block_on_send`; `ipc_port_enqueue_wait(SEND)` → `schedule()` → return `REND_SUCCESS`.

IPC does **not** `schedule()` the peer; it only sets peer to `ready`.

### 6.2 `ipc_try_send_msg(port)`

Steps 1–2 identical to §6.1 matched path. Step 3 replaced by: **not matched → return `-E_REND_AGAIN`** (message remains on send queue).

| Return | Condition |
|--------|-----------|
| `REND_SUCCESS` | Transfer OK |
| `-E_REND_AGAIN` | `try_match` NULL, or no current thread |
| `-E_IN_PARAM` | `port == NULL` |
| Other | From `ipc_transfer_message` |

### 6.3 `recv_msg(port)`

1. `ipc_port_try_match(port, IPC_PORT_STATE_RECV)`.
2. **Matched:** `ipc_transfer_message(sender, receiver)`.
   - `REND_SUCCESS` → sender `block_on_send` → `ready`; `ref_put`; return.
   - `-E_REND_NO_MSG` → `continue`.
   - `-E_REND_AGAIN` → `ref_put`; return `-E_RENDEZVOS`.
   - Else → `ref_put`; return error.
3. **Not matched:** `block_on_receive`; `enqueue_wait(RECV)` → `schedule()` → return `REND_SUCCESS`.

**Caller after success:** `dequeue_recv_msg()`.

### 6.4 `ipc_try_recv_msg(port)`

Same as §6.3 step 2; not matched → `-E_REND_AGAIN`. Same return table as §6.3 except no blocking path.

### 6.5 `while (1)` on try APIs

Retries **only** after a successful `try_match`, for the same reasons as blocking APIs:

- Send path: `ipc_transfer_message` → `-E_REND_AGAIN`.
- Recv path: `ipc_transfer_message` → `-E_REND_NO_MSG`.

Not a spin-wait on an empty port.

---

## 7. Cancel / abort (no `cancel_ipc`)

| Fact | Detail |
|------|--------|
| No API | `cancel_ipc` is not implemented and not planned |
| Wakeup mechanism | Peer completes `ipc_transfer_message`; waiter → `ready` |
| Abort pattern | Dedicated port + protocol message; timer cancel uses `rendezvos_timer_event_cancel()` → `ipc_system_try_deliver(..., false)` (see §10); other cases may use `send_msg` / `ipc_try_send_msg` after `enqueue_msg_for_send` |
| Stale waiters | `ipc_port_try_match` drops requests if thread status ≠ expected block state |

Rationale and alternatives: [`lockfree-ipc.md`](lockfree-ipc.md) §8.1.

---

## 8. Call patterns (copy-paste templates)

**Server (blocking recv):**

```c
for (;;) {
        if (recv_msg(port) != REND_SUCCESS)
                break;
        Message_t *m = dequeue_recv_msg();
        if (!m)
                continue;
        /* handle m; ref_put / free per message.h */
}
```

**Client send:**

```c
/* build Message_t + kmsg payload */
enqueue_msg_for_send(msg);
send_msg(port);   /* or ipc_try_send_msg(port) */
```

**Non-blocking send (ordinary thread, peer may be absent):**

```c
enqueue_msg_for_send(msg);
error_t r = ipc_try_send_msg(port);
if (r == -E_REND_AGAIN) {
        /* No rendezvous: msg still on this thread's send_msg_queue */
} else if (r != REND_SUCCESS) {
        /* handle error */
}
```

**Timer expire from IRQ (`use_system_proxy = true`):**

```c
Message_t *msg = /* build TIMER_EXPIRE payload (delivery_token) */;
error_t r = ipc_system_try_deliver(wait_port, msg, true);
/* Stages on idle persona; set_cpu_current_thread(proxy) for try_send; try-clean on fail. */
```

**Timer cancel (thread context, `use_system_proxy = false`):**

```c
/* Prefer the core helper (disarm + build TIMER_CANCEL kmsg + deliver): */
error_t r = rendezvos_timer_event_cancel(&ev);

/* Equivalent manual path: */
Message_t *msg = /* build TIMER_CANCEL payload (delivery_token) */;
error_t r = ipc_system_try_deliver(wait_port, msg, false);
/* Stages on current thread send_pending_msg; no enqueue; try-clean on fail. */
```

**Device IRQ to bound driver kthread (known receiver):**

```c
for (each pending packet) {
        Message_t *msg = /* build */;
        error_t r = ipc_system_deliver_to(nic_kthread, msg, true);
        /* Loop: one in-flight on sender send_pending_msg at a time; try-clean on failure. */
}

/* nic_kthread (thread context): */
while ((m = dequeue_recv_msg()) != NULL) {
        handle_rx(m);
        ref_put(&m->ms_queue_node.refcount, free_message_ref);
}
```

---

## 9. Invariants (for reviewers / AI)

1. **Never** pass `Message_t*` to `send_msg` / `ipc_try_send_msg`.
2. **Always** `enqueue_msg_for_send` before ordinary send APIs (not before `ipc_system_*`).
3. **Always** `dequeue_recv_msg` after recv APIs return `REND_SUCCESS`.
4. `-E_REND_AGAIN` from `ipc_try_send_msg` means **no transfer occurred** — not implicit success.
5. Do **not** modify `send_msg`/`recv_msg` when adding features; add try paths or new functions.
6. Do **not** implement Linux-style forced dequeue of blocked threads from port MS-queue.
7. **System async:** at most one outbound message staged on the chosen sender's `send_pending_msg` at a time; never `enqueue_msg_for_send` on these paths; try-clean on failure (§10).

---

## 10. System async outbound (IRQ / trap)

Design rationale and IRQ sender model: [`lockfree-ipc.md`](lockfree-ipc.md) §9. This section is the **API contract** for AI and call sites.

### 10.1 Problem

Timer and device IRQ paths have **no legitimate sender** `Thread_Base*`. Using `get_cpu_current_thread()` would pollute the interrupted thread’s `send_msg_queue` (transfer always takes the **queue head**, not a newly enqueued tail message).

**Solution:** stage outbound messages in the sender's `send_pending_msg` (single slot, compatible with `ipc_transfer_message`). Pick sender via `use_system_proxy`:

- **`true`:** per-CPU idle persona (`idle_thread_ptr`); `try_deliver` also `set_cpu_current_thread(proxy)` around `ipc_try_send_msg` (see `get_cpu_current_thread` / `set_cpu_current_thread` in `task/tcb.h`).
- **`false`:** current thread (`get_cpu_current_thread()`); no `current_thread` change; do **not** `enqueue_msg_for_send` — staging only (e.g. timer cancel).

**Inbound core mail** (something sends *to* core) uses a **dedicated kthread** + blocking `recv_msg` on its port (same as `powerd` / servers). There is **no** `ipc_system_try_recv` — persona recv in IRQ was intentionally omitted.

### 10.2 Two outbound models

| | `ipc_system_try_deliver(port, msg, proxy)` | `ipc_system_deliver_to(receiver, msg, proxy)` |
|---|-------------------------------------|----------------------------------------|
| **Use case** | Timer expire (proxy true) / cancel (proxy false) | NIC / device IRQ → driver kthread (proxy true) |
| **Sender** | Idle persona if `proxy`; else current thread | Same |
| **Find receiver** | `ipc_try_send_msg(port)` → port try_match | Caller passes `Thread_Base*` (bound at init) |
| **Receiver must block on port?** | **Yes** (try_deliver only) | **No** — drains recv queue in thread context |
| **`current_thread` switch** | `set_cpu_current_thread(proxy)` only when `proxy == true` (try_deliver) | Never |

```mermaid
flowchart LR
  subgraph irq [IRQ / trap or thread cancel]
    PND[pending / cancel call]
    STG["CAS → sender.send_pending_msg"]
  end

  subgraph mode1 [try_deliver]
    PORT[wait_port try_match]
    WAKE[receiver → ready]
  end

  subgraph mode2 [deliver_to]
    TCB[known Thread_Base]
    RQ[receiver recv_msg_queue]
  end

  PND --> STG
  STG -->|port path| PORT --> WAKE
  STG -->|direct path| TCB --> RQ
```

### 10.3 Shared rules (both APIs)

1. **Stage:** `atomic64_cas(sender->send_pending_msg, NULL, msg)` on chosen sender — slot must be empty; else `-E_REND_IPC`.
2. **Transfer:** one message per call; batch by looping (each: stage → transfer → success or clean → next).
3. **Try-clean:** on failure after staging, `exchange(send_pending_msg, NULL)` + `ref_put(msg)`.
4. **Ownership:** caller transfers `msg` on entry; failure releases via try-clean; success consumed by transfer.
5. **Do not** `enqueue_msg_for_send` for these paths — use staging only.
6. **Do not** expose raw `Thread_Base*` on `send_msg` / `ipc_try_send_msg`; `ipc_system_deliver_to` is the explicit-receiver async entry.

### 10.4 `ipc_system_try_deliver(port, msg, use_system_proxy)`

1. Validate `port`, `msg`, refcounts.
2. `curr = get_cpu_current_thread()`, `proxy = idle_thread_ptr`, `sender = use_system_proxy ? proxy : curr`.
3. If `!sender` → `-E_REND_AGAIN`. If `use_system_proxy && !core_tm` → `-E_IN_PARAM` (before staging).
4. CAS stage on `sender->send_pending_msg`; slot busy → `-E_REND_IPC`.
5. If `use_system_proxy`: `set_cpu_current_thread(proxy)`.
6. `ipc_try_send_msg(port)`; if step 5 ran: `set_cpu_current_thread(curr)`.
7. On transfer failure: exchange `send_pending_msg` + `ref_put(msg)`.

| Return | Meaning |
|--------|---------|
| `REND_SUCCESS` | Transfer OK; port waiter likely `ready` |
| `-E_REND_AGAIN` | No receiver on port, or no sender (no proxy / no current thread) |
| `-E_REND_IPC` | Slot busy or invalid message refs |
| `-E_IN_PARAM` | NULL `port`/`msg`, or proxy path without `core_tm` |

Stale expire/cancel when waiter already left the port: `-E_REND_AGAIN` + try-clean — expected; waiter validates `delivery_token` (compat layer).

**Timer (one-shot, model B port ref):** `rendezvos_timer_event_init` ref_get; `fini` ref_put (one-shot only — never `fini` periodic heartbeat). `del` / `cancel` do not release the port ref. EXPIRE: IRQ calls `ipc_system_try_deliver(..., true)`. CANCEL: `rendezvos_timer_event_cancel()` → `ipc_system_try_deliver(..., false)`.

### 10.5 `ipc_system_deliver_to(receiver, msg, use_system_proxy)`

1. Validate `receiver`, `msg`, refcounts.
2. `sender = use_system_proxy ? idle_thread_ptr : get_cpu_current_thread()`.
3. If `!sender` → `-E_REND_AGAIN`.
4. CAS stage on `sender->send_pending_msg`; slot busy → `-E_REND_IPC`.
5. `ipc_transfer_message(sender, receiver)`.
6. On transfer failure: exchange `send_pending_msg` + `ref_put(msg)`.

No `set_cpu_current_thread` on this path (proxy or not).

| Return | Meaning |
|--------|---------|
| `REND_SUCCESS` | Copy on `receiver->recv_msg_queue`; `recv_pending_cnt` incremented in transfer |
| `-E_REND_AGAIN` | No sender, or transfer failed (e.g. receiver exiting) |
| `-E_REND_IPC` | Slot busy or invalid refs |
| `-E_IN_PARAM` | NULL `receiver` or `msg` |

Receiver **must** poll `dequeue_recv_msg()` (and `ref_put`) in thread context; message may arrive while receiver is handling sends.

### 10.6 `recv_pending_cnt`

- `ipc_transfer_message` increments `receiver->recv_pending_cnt` **after** successful enqueue to recv queue.
- `dequeue_recv_msg` decrements it. Pairing applies to all receivers including `deliver_to` targets.

---

## 11. Related files

| File | Content |
|------|---------|
| `kernel/ipc/ipc.c` | `send_msg`, `recv_msg`, `ipc_try_*`, `ipc_system_*`, `ipc_transfer_message` |
| `include/rendezvos/ipc/ipc.h` | Public declarations (incl. system async §10) |
| `kernel/ipc/message.c` | `Message_t` lifecycle |
| `include/rendezvos/ipc/port.h` | `Message_Port_t`, port table |
| `include/rendezvos/task/tcb.h` | `get_cpu_current_thread`, `set_cpu_current_thread` |
| `kernel/time/time.c` | Timer queue; `rendezvos_timer_event_cancel` |
| `include/rendezvos/ipc/kmsg.h` | `KMSG_OP_CORE_TIMER_EXPIRE`, `KMSG_OP_CORE_TIMER_CANCEL` |
| `lockfree-ipc.md` | Design document (authoritative “why”, §9 IRQ sender) |
| `task-thread.md` | `thread_set_status`, `schedule`, teardown |

---

## 12. kmsg (minimal)

- Create payloads: `kmsg_create(module, opcode, fmt, ...)`.
- Client and server **must** use the same `fmt` for a given opcode.
- Reply port name: conventionally embedded in TLV stream.
- Core timer opcodes: `KMSG_OP_CORE_TIMER_EXPIRE` (IRQ delivery), `KMSG_OP_CORE_TIMER_CANCEL` (thread-context cancel); payload includes `delivery_token` for waiter validation.

Details: `ipc/kmsg.h`, `ipc/ipc_serial.h`, [`GUIDE.md`](GUIDE.md) §10 for `error_t`.
