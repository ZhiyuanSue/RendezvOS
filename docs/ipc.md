# IPC (ports and messages)

Core inter-thread communication: ports, queues, and serialized payloads.

External callers: [`USING_CORE.md`](USING_CORE.md) §3.1 · Design: [`lockfree-ipc.md`](lockfree-ipc.md)

---

## Components

| Piece | Header | Role |
|-------|--------|------|
| Port | `ipc/port.h` | Named endpoint, `service_id`, thread wait queue |
| Transport | `ipc/ipc.h` | Send and receive between threads |
| Message | `ipc/message.h` | Message object lifetime |
| Wire payload | `ipc/kmsg.h`, `ipc/ipc_serial.h` | `kmsg_create`, TLV measure/encode/decode |
| Name table | `registry/name_index.h` | String → object (used by global port table) |

Each port may carry a `service_id` used as the `kmsg` module field for fast “intended recipient?” checks. Routing and discovery use the port name string.

### Port table (global)

```c
extern struct Port_Table* global_port_table;

Message_Port_t* port_table_lookup(struct Port_Table* table, const char* name);
error_t register_port(struct Port_Table* table, Message_Port_t* port);
```

`global_port_init()` during boot registers the table. Look up by **name**; bind `service_id` on the port before sending `kmsg` traffic.

### Send / receive sequence

**Sender**

1. Build `Message_t` / attach `kmsg` payload.
2. `enqueue_msg_for_send(msg)` — current thread’s send queue.
3. `send_msg(port)` — may block until a receiver is connected.

**Receiver**

1. `recv_msg(port)` — may block.
2. `msg = dequeue_recv_msg()` — real message (not MS-queue dummy).
3. Process; `ref_dec` / free per message lifetime rules.

**Cancel:** `cancel_ipc(target_thread)` is declared but **not implemented** in tree (stub commented in `ipc.c`). Use thread exit / teardown and matcher discard for blocked peers until `cancel_ipc` is wired.

Errors: `-E_REND_NO_MSG`, `-E_REND_AGAIN` — see [`GUIDE.md`](GUIDE.md) §10.

---

## kmsg and TLV

- Allocate and fill payloads with `kmsg_create(module, opcode, fmt, ...)` (variadic; do not pass an external `va_list` unless using the dedicated VA helpers in `ipc_serial.h`).
- Client and server **must share the same `fmt` and argument types** for a given opcode (same rules as `printf`).
- Reply routing: conventionally encode the reply port name in the TLV stream; the receiver sends the response to that port.

See `ipc/kmsg.h` and `ipc/ipc_serial.h` for layout (`offsetof(kmsg_t, payload)` for size calculations).

---

## Server threads

A typical kernel service thread:

1. Create or look up a port (`port.h` APIs).
2. Loop: receive message → dispatch by `opcode` → optionally send reply to the port named in the request.
3. On shutdown: stop the receive loop, drop port registrations, then tear down the thread (`task-thread.md`).

Blocking: the receiver uses `thread_set_status` + `schedule` while waiting; the sender may block until the message is accepted or queued per `ipc.c` semantics.

---

## Failure paths

- Decode errors: do not leave a peer blocked on `recv` without a defined error reply or explicit drop policy.
- Port refcount: symmetric acquire/release; detach from traversable structures before last `ref_put`.
- `service_id` on the port should match the `module` field in outgoing `kmsg` headers.

---

## See also

- [`task-thread.md`](task-thread.md) — blocking and server threads
- [`lockfree-ipc.md`](lockfree-ipc.md) — MS queue and design rationale
