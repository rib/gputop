#include "gputop-client-context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gputop-gens-metrics.h"

#include "gputop-log.h"

#include "main/hash.h" /* For uint_key() */
#include "util/ralloc.h"

static void i915_perf_empty_samples(struct gputop_client_context *ctx);
static void clear_perf_tracepoints_data(struct gputop_client_context *ctx);
static void delete_process_entry(struct hash_entry *entry);

int
gputop_client_pretty_print_value(gputop_counter_units_t unit,
                                 double value, char *buffer, size_t length)
{
    static const char *times[] = { "ns", "us", "ms", "s" };
    static const char *bytes[] = { "B", "KiB", "MiB", "GiB" };
    static const char *freqs[] = { "Hz", "KHz", "MHz", "GHz" };
    static const char *texels[] = { "texels", "K texels", "M texels", "G texels" };
    static const char *pixels[] = { "pixels", "K pixels", "M pixels", "G pixels" };
    static const char *cycles[] = { "cycles", "K cycles", "M cycles", "G cycles" };
    static const char *threads[] = { "threads", "K threads", "M threads", "G threads" };
    const char **scales = NULL;

    switch (unit) {
    case GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES:   scales = bytes; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_HZ:      scales = freqs; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_NS:
    case GPUTOP_PERFQUERY_COUNTER_UNITS_US:      scales = times; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_PIXELS:  scales = pixels; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_TEXELS:  scales = texels; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_THREADS: scales = threads; break;
    case GPUTOP_PERFQUERY_COUNTER_UNITS_CYCLES:  scales = cycles; break;
    default: break;
    }

    int l;
    if (scales) {
        const double base = unit == GPUTOP_PERFQUERY_COUNTER_UNITS_BYTES ? 1024 : 1000;

        if (unit == GPUTOP_PERFQUERY_COUNTER_UNITS_US)
            value *= 1000;

        int i = 0;
        while (value >= base && i < 3) {
            value /= base;
            i++;
        }
        l = snprintf(buffer, length, "%.4g %s", value, scales ? scales[i] : "");
    } else {
        if (unit == GPUTOP_PERFQUERY_COUNTER_UNITS_PERCENT)
            l = snprintf(buffer, length, "%.3g %%", value);
        else
            l = snprintf(buffer, length, "%.2f", value);
    }

    return l;
}

double gputop_client_context_max_value(struct gputop_client_context *ctx,
                                       const struct gputop_metric_set_counter *counter,
                                       uint64_t ns_time)
{
    if (!counter->max_uint64 && !counter->max_float)
        return 0.0f;

    uint32_t counters0[64] = { 0, 1} ;
    uint32_t counters1[64] = { 0, 1 + gputop_time_scale_timebase(&ctx->devinfo, ns_time),
                               0, gputop_time_scale_timebase(&ctx->devinfo, ns_time), };
    struct gputop_cc_oa_accumulator dummy_accumulator;
    gputop_cc_oa_accumulator_init(&dummy_accumulator, &ctx->devinfo,
                                  counter->metric_set, 0, NULL);
    gputop_cc_oa_accumulate_reports(&dummy_accumulator,
                                    (uint8_t *) counters0, (uint8_t *) counters1);

    double value;

    switch (counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        value = counter->max_uint64(&ctx->devinfo,
                                    counter->metric_set,
                                    dummy_accumulator.deltas);
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        value = counter->max_float(&ctx->devinfo,
                                   counter->metric_set,
                                   dummy_accumulator.deltas);
        break;
    }

    return value;
}

int
gputop_client_context_pretty_print_max(struct gputop_client_context *ctx,
                                       const struct gputop_metric_set_counter *counter,
                                       uint64_t ns_time, char *buffer, size_t length)
{
    double value = gputop_client_context_max_value(ctx, counter, ns_time);
    if (value == 0.0f)
        return snprintf(buffer, length, "unknown");

    return gputop_client_pretty_print_value(counter->units, value, buffer, length);
}

/**/

static void
generate_uuid(struct gputop_client_context *ctx, char *out, size_t size, void *ptr)
{
    snprintf(out, size, "%p", ptr);
}

/**/

struct protobuf_msg_closure;

static void
send_pb_message(struct gputop_client_context *ctx, ProtobufCMessage *pb_message)
{
    if (!ctx->connection)
        return;

    size_t len = protobuf_c_message_get_packed_size(pb_message);
    uint8_t *data = (uint8_t *) malloc(len);
    protobuf_c_message_pack(pb_message, data);
    gputop_connection_send(ctx->connection, data, len);
    free(data);
}

/**/

static struct gputop_process_info *
get_process_info(struct gputop_client_context *ctx, uint32_t pid)
{
    struct hash_entry *entry =
        pid == 0 ? NULL :
        _mesa_hash_table_search(ctx->pid_to_process_table, uint_key(pid));
    if (entry || pid == 0)
        return entry ? ((struct gputop_process_info *) entry->data) : NULL;

    struct gputop_process_info *info = (struct gputop_process_info *) calloc(1, sizeof(*info));
    info->pid = pid;
    snprintf(info->cmd, sizeof(info->cmd), "<unknown>");
    _mesa_hash_table_insert(ctx->pid_to_process_table, uint_key(pid), info);

    list_addtail(&info->link, &ctx->process_infos);

    Gputop__Request request = GPUTOP__REQUEST__INIT;
    request.req_case = GPUTOP__REQUEST__REQ_GET_PROCESS_INFO;
    request.get_process_info = pid;
    send_pb_message(ctx, &request.base);

    return info;
}

static void
update_hw_contexts_process_info(struct gputop_client_context *ctx,
                                struct gputop_process_info *process)
{
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
        if (context->process != process)
            continue;

        snprintf(context->name, sizeof(context->name),
                 "%s id=%u", process->cmd, context->hw_id);
    }
}

/**/

static void
put_i915_perf_chunk(struct gputop_i915_perf_chunk *chunk)
{
    if (!chunk || --chunk->refcount)
        return;

    list_del(&chunk->link);
    free(chunk);
}

static struct gputop_i915_perf_chunk *
get_i915_perf_chunk(struct gputop_client_context *ctx,
                    const uint8_t *data, size_t len)
{
    struct gputop_i915_perf_chunk *chunk =
        (struct gputop_i915_perf_chunk *) malloc(len + sizeof(*chunk));

    memcpy(chunk->data, data, len);
    chunk->length = len;

    chunk->refcount = 1;
    list_addtail(&chunk->link, &ctx->i915_perf_chunks);

    return chunk;
}

static struct gputop_i915_perf_chunk *
ref_i915_perf_chunk(struct gputop_i915_perf_chunk *chunk)
{
    chunk->refcount++;
    return chunk;
}

static uint64_t
i915_perf_timestamp(struct gputop_client_context *ctx,
                    const struct drm_i915_perf_record_header *header)
{
    if (ctx->i915_perf_config.cpu_timestamps) {
        return *((uint64_t *) gputop_i915_perf_record_field(
                     &ctx->i915_perf_config, header,
                     GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP));
    }

    uint64_t ts =
        gputop_cc_oa_report_get_timestamp(
            gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT));
    uint64_t prev_ts = ctx->last_header ?
        gputop_cc_oa_report_get_timestamp(
            gputop_i915_perf_record_field(&ctx->i915_perf_config, ctx->last_header,
                                          GPUTOP_I915_PERF_FIELD_OA_REPORT)) : ts;

    ts = ctx->last_oa_timestamp + gputop_timebase_scale_ns(&ctx->devinfo, ts - prev_ts);

    return ts;
}

/**/

static void
open_stream(struct gputop_stream *stream,
            struct gputop_client_context *ctx,
            Gputop__OpenStream *pb_open_stream)
{
    stream->id = pb_open_stream->id = ctx->stream_id++;
    list_add(&stream->link, &ctx->streams);

    Gputop__Request request = GPUTOP__REQUEST__INIT;
    request.req_case = GPUTOP__REQUEST__REQ_OPEN_STREAM;
    request.open_stream = pb_open_stream;

    send_pb_message(ctx, &request.base);
}

static void
close_stream(struct gputop_stream *stream,
             struct gputop_client_context *ctx)
{
    if (stream->id == 0)
        return;

    Gputop__Request request = GPUTOP__REQUEST__INIT;
    request.req_case = GPUTOP__REQUEST__REQ_CLOSE_STREAM;
    request.close_stream = stream->id;
    send_pb_message(ctx, &request.base);

    list_del(&stream->link);
    memset(stream, 0, sizeof(*stream));
}

static bool
is_stream_opened(struct gputop_stream *stream)
{
    return stream->id != 0;
}

static struct gputop_stream *
find_stream(struct gputop_client_context *ctx, uint32_t stream_id)
{
    list_for_each_entry(struct gputop_stream, stream, &ctx->streams, link) {
        if (stream->id == stream_id)
            return stream;
    }

    return NULL;
}

/**/

struct gputop_perf_tracepoint *
gputop_client_context_add_tracepoint(struct gputop_client_context *ctx, const char *name)
{
    struct hash_entry *entry =
        _mesa_hash_table_search(ctx->perf_tracepoints_name_table, name);
    if (entry)
        return (struct gputop_perf_tracepoint *) entry->data;

    struct gputop_perf_tracepoint *tp =
        (struct gputop_perf_tracepoint *) calloc(1, sizeof(*tp));

    tp->hw_id_field = tp->process_field = -1;
    tp->idx = list_length(&ctx->perf_tracepoints);
    list_inithead(&tp->streams);
    list_inithead(&tp->data);

    snprintf(tp->name, sizeof(tp->name), "%s", name);
    generate_uuid(ctx, tp->uuid, sizeof(tp->uuid), tp);

    list_addtail(&tp->link, &ctx->perf_tracepoints);
    _mesa_hash_table_insert(ctx->perf_tracepoints_uuid_table, tp->uuid, tp);
    _mesa_hash_table_insert(ctx->perf_tracepoints_name_table, tp->name, tp);

    Gputop__Request request = GPUTOP__REQUEST__INIT;
    request.uuid = tp->uuid;
    request.req_case = GPUTOP__REQUEST__REQ_GET_TRACEPOINT_INFO;
    request.get_tracepoint_info = tp->name;
    send_pb_message(ctx, &request.base);

    return tp;
}

void
gputop_client_context_print_tracepoint_data(struct gputop_client_context *ctx,
                                            char *buf, size_t len,
                                            struct gputop_perf_tracepoint_data *data,
                                            bool include_name)
{
    struct gputop_perf_tracepoint *tp = data->tp;
    int l;

    if (include_name) {
        l = snprintf(buf, len, "%s: ", data->tp->name);
        buf += l;
        len -= l;
    }

    l = snprintf(buf, len, "cpu:%i", data->cpu);
    buf += l;
    len -= l;

    for (int f = 0; f < tp->n_fields; f++) {
        const char *name = tp->fields[f].name;

        if (!strcmp("common_type", name) ||
            !strcmp("common_flags", name) ||
            !strcmp("common_preempt_count", name))
            continue;

        void *value_ptr = &data->data.data[tp->fields[f].offset];
        l = 0;
        if (!strcmp("common_pid", name)) {
            uint32_t pid = *((uint32_t *) value_ptr);
            struct hash_entry *entry =
                _mesa_hash_table_search(ctx->pid_to_process_table, uint_key(pid));
            l = snprintf(buf, len, "\npid = %u(%s)", pid,
                         entry ? ((struct gputop_process_info *)entry->data)->cmd : "<unknown>");
        } else {
            switch (tp->fields[f].size) {
            case 1:
                if (tp->fields[f].is_signed)
                    l = snprintf(buf, len, "\n%s = %hhd", name, *((int8_t *) value_ptr));
                else
                    l = snprintf(buf, len, "\n%s = %hhu", name, *((uint8_t *) value_ptr));
                break;
            case 2:
                if (tp->fields[f].is_signed)
                    l = snprintf(buf, len, "\n%s = %hd", name, *((int16_t *) value_ptr));
                else
                    l = snprintf(buf, len, "\n%s = %hu", name, *((uint16_t *) value_ptr));
                break;
            case 4:
                if (tp->fields[f].is_signed)
                    l = snprintf(buf, len, "\n%s = %d", name, *((int32_t *) value_ptr));
                else
                    l = snprintf(buf, len, "\n%s = %u", name, *((uint32_t *) value_ptr));
                break;
            case 8:
                if (tp->fields[f].is_signed)
                    l = snprintf(buf, len, "\n%s = %" PRIi64, name, *((int64_t *) value_ptr));
                else
                    l = snprintf(buf, len, "\n%s = %" PRIu64, name, *((uint64_t *) value_ptr));
                break;
            }
        }

        if (l > 0) {
            buf += l;
            len -= l;
        }
    }
}

union value {
    char *string;
    int integer;
};

struct parser_ctx {
    struct gputop_perf_tracepoint *tp;
    char *buffer;
    size_t len;
    int pos;
};

#define YY_CTX_LOCAL
#define YY_CTX_MEMBERS struct parser_ctx ctx;
#define YYSTYPE union value
#define YY_LOCAL(T) static __attribute__((unused)) T
#define YY_PARSE(T) static T
#define YY_INPUT(yy, buf, result, max)                  \
    {                                                   \
        int c = yy->ctx.pos < yy->ctx.len ?             \
            yy->ctx.buffer[yy->ctx.pos++] : EOF;        \
        result = (EOF == c) ? 0 : (*(buf) = c, 1);      \
    }

#include "tracepoint_format.leg.h"

static void
update_tracepoint(struct gputop_perf_tracepoint *tp,
                  const Gputop__TracepointInfo *info)
{
    assert(tp->n_fields == 0);

    tp->event_id = info->event_id;
    tp->format = strdup(info->sample_format);
    tp->hw_id_field = tp->process_field = -1;

    yycontext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ctx.tp = tp;
    ctx.ctx.buffer = tp->format;
    ctx.ctx.len = strlen(tp->format);
    if (yyparse(&ctx)) {
        /* Only use i915_gem_request_add or i915_context_create for process
         * correlation. */
        if (!strcmp(tp->name, "i915/i915_request_add") ||
            !strcmp(tp->name, "i915/i915_request_submit") ||
            !strcmp(tp->name, "i915/i915_context_create")) {
            for (int f = 0; f < tp->n_fields; f++) {
                if (!strcmp(tp->fields[f].name, "common_pid")) {
                    tp->process_field = f;
                } else if (!strcmp(tp->fields[f].name, "hw_id")) {
                    tp->hw_id_field = f;
                }
            }
        }
    } else
        tp->n_fields = 0;
    yyrelease(&ctx);
}

static void
add_tracepoint_stream_data(struct gputop_client_context *ctx,
                           struct gputop_perf_tracepoint_stream *stream,
                           const uint8_t *data, size_t len)
{
    struct gputop_perf_tracepoint *tp = stream->tp;
    struct gputop_perf_tracepoint_data *tp_data =
        (struct gputop_perf_tracepoint_data *) malloc(sizeof(*tp_data) - sizeof(tp_data->data) + len);
    tp_data->tp = tp;
    tp_data->cpu = stream->cpu;
    memcpy(&tp_data->data, data, len);

    /* Reunify the per cpu data into one global stream of tracepoints sorted
     * by time. */
    struct gputop_perf_tracepoint_data *tp_end_data =
        list_empty(&ctx->perf_tracepoints_data) ?
        NULL : list_last_entry(&ctx->perf_tracepoints_data,
                               struct gputop_perf_tracepoint_data, link);
    while (tp_end_data && tp_end_data->data.time > tp_data->data.time) {
        tp_end_data = (tp_end_data->link.prev == &ctx->perf_tracepoints_data) ? NULL :
            LIST_ENTRY(struct gputop_perf_tracepoint_data, tp_end_data->link.prev, link);
    }
    list_add(&tp_data->link, tp_end_data ? &tp_end_data->link : &ctx->perf_tracepoints_data);

    /* Also reunify the per cpu data into the stream of tracepoints sorted by
     * time. */
    tp_end_data = list_empty(&tp->data) ?
        NULL : list_last_entry(&tp->data, struct gputop_perf_tracepoint_data, tp_link);
    while (tp_end_data && tp_end_data->data.time > tp_data->data.time) {
        tp_end_data = (tp_end_data->tp_link.prev == &tp->data) ? NULL :
            LIST_ENTRY(struct gputop_perf_tracepoint_data, tp_end_data->tp_link.prev, tp_link);
    }
    list_add(&tp_data->tp_link, tp_end_data ? &tp_end_data->tp_link : &tp->data);

    /* Remove tracepoints outside the sampling window. */
    const uint64_t max_length = ctx->oa_visible_timeline_s * 1000000000ULL;
    struct gputop_perf_tracepoint_data *tp_start_data =
        list_first_entry(&ctx->perf_tracepoints_data, struct gputop_perf_tracepoint_data, link);
    tp_end_data =
        list_last_entry(&ctx->perf_tracepoints_data, struct gputop_perf_tracepoint_data, link);

    while ((tp_end_data->data.time - tp_start_data->data.time) > max_length) {
        list_del(&tp_start_data->link);
        list_del(&tp_start_data->tp_link);
        free(tp_start_data);
        tp_start_data = list_first_entry(&ctx->perf_tracepoints_data,
                                         struct gputop_perf_tracepoint_data, link);
    }

    if (tp->process_field >= 0) {
        uint32_t pid = *((uint32_t *)&tp_data->data.data[tp->fields[tp->process_field].offset]);
        struct gputop_process_info *process = get_process_info(ctx, pid);

        if (process && tp->hw_id_field >= 0) {
            uint32_t hw_id = *((uint32_t *)&tp_data->data.data[tp->fields[tp->hw_id_field].offset]);
            _mesa_hash_table_insert(ctx->hw_id_to_process_table, uint_key(hw_id), process);

            struct hash_entry *entry =
                _mesa_hash_table_search(ctx->hw_contexts_table, uint_key(hw_id));
            if (entry) {
                struct gputop_hw_context *context = (struct gputop_hw_context *) entry->data;
                if (context->process != process) {
                    context->process = process;
                    if (process->cmd_line[0] != '\0')
                        update_hw_contexts_process_info(ctx, process);
                }
            }
        }
    }
}

static void
close_perf_tracepoint(struct gputop_client_context *ctx, struct gputop_perf_tracepoint *tp)
{
    list_for_each_entry_safe(struct gputop_perf_tracepoint_stream, stream, &tp->streams, link) {
        list_del(&stream->link);
        _mesa_hash_table_remove(ctx->perf_tracepoints_stream_table,
                                _mesa_hash_table_search(ctx->perf_tracepoints_stream_table,
                                                        uint_key(stream->base.id)));
        close_stream(&stream->base, ctx);
        free(stream);
    }
}

static void
open_perf_tracepoint(struct gputop_client_context *ctx, struct gputop_perf_tracepoint *tp)
{
    close_perf_tracepoint(ctx, tp);

    Gputop__TracepointConfig pb_tp_config = GPUTOP__TRACEPOINT_CONFIG__INIT;
    pb_tp_config.pid = -1;
    pb_tp_config.id = tp->event_id;

    Gputop__OpenStream pb_stream = GPUTOP__OPEN_STREAM__INIT;
    pb_stream.overwrite = false;
    pb_stream.live_updates = true;
    pb_stream.type_case = GPUTOP__OPEN_STREAM__TYPE_TRACEPOINT;
    pb_stream.tracepoint = &pb_tp_config;

    for (int cpu = 0; cpu < ctx->features->features->n_cpus; cpu++) {
        pb_tp_config.cpu = cpu;

        struct gputop_perf_tracepoint_stream *stream =
            (struct gputop_perf_tracepoint_stream *) calloc(1, sizeof(struct gputop_perf_tracepoint_stream));
        stream->tp = tp;
        stream->cpu = cpu;
        open_stream(&stream->base, ctx, &pb_stream);

        list_add(&stream->link, &tp->streams);
        _mesa_hash_table_insert(ctx->perf_tracepoints_stream_table,
                                uint_key(stream->base.id), stream);
    }
}

void
gputop_client_context_remove_tracepoint(struct gputop_client_context *ctx,
                                        struct gputop_perf_tracepoint *tp)
{
    close_perf_tracepoint(ctx, tp);
    if (tp->format)
        free(tp->format);
    list_del(&tp->link);
    free(tp);

    int idx = 0;
    list_for_each_entry(struct gputop_perf_tracepoint, ltp, &ctx->perf_tracepoints, link)
        ltp->idx = idx++;
}

/**/

double
gputop_client_context_calc_busyness(struct gputop_client_context *ctx)
{
    double total = 0.0f;

    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link)
        total += context->usage_percent;

    return total;
}

uint64_t
gputop_client_context_convert_gt_timestamp(struct gputop_client_context *ctx,
                                           uint32_t gt_timestamp)
{
    list_for_each_entry(struct gputop_accumulated_samples, samples, &ctx->timelines, link) {
        uint32_t start_gt_ts =
            gputop_i915_perf_record_timestamp(&ctx->i915_perf_config,
                                              samples->start_report.header);
        uint32_t end_gt_ts =
            gputop_i915_perf_record_timestamp(&ctx->i915_perf_config,
                                              samples->end_report.header);

        if (end_gt_ts < gt_timestamp)
            continue;
        if (start_gt_ts > gt_timestamp)
            return 0ULL;

        uint32_t gt_delta = gt_timestamp - start_gt_ts;
        uint64_t delta = gt_delta * (samples->timestamp_end - samples->timestamp_start) /
            (end_gt_ts - start_gt_ts);

        return samples->timestamp_start + delta;
    }

    return 0ULL;
}

static struct gputop_accumulated_samples *
get_accumulated_sample(struct gputop_client_context *ctx,
                       struct gputop_i915_perf_chunk *chunk,
                       const struct drm_i915_perf_record_header *header,
                       uint32_t hw_id);
static void put_accumulated_sample(struct gputop_client_context *ctx,
                                   struct gputop_accumulated_samples *samples);

static void
hw_context_update_process(struct gputop_client_context *ctx,
                          struct gputop_hw_context *context)
{
    if (context->process)
        return;

    struct hash_entry *process_entry =
        _mesa_hash_table_search(ctx->hw_id_to_process_table, uint_key(context->hw_id));
    if (process_entry) {
        context->process = (struct gputop_process_info *) process_entry->data;
        snprintf(context->name, sizeof(context->name),
                 "%s id=%u", context->process->cmd, context->hw_id);
    }
}

static struct gputop_hw_context *
get_hw_context(struct gputop_client_context *ctx, uint32_t hw_id)
{
    struct hash_entry *hw_entry =
        _mesa_hash_table_search(ctx->hw_contexts_table, uint_key(hw_id));

    struct gputop_hw_context *new_context;
    if (hw_entry) {
        new_context = (struct gputop_hw_context *) hw_entry->data;
        new_context->n_samples++;

        hw_context_update_process(ctx, new_context);

        return new_context;
    }

    new_context = (struct gputop_hw_context *) calloc(1, sizeof(*new_context));
    snprintf(new_context->name, sizeof(new_context->name), "<unknown> id=%u", hw_id);
    new_context->hw_id = hw_id;
    new_context->timeline_row = _mesa_hash_table_num_entries(ctx->hw_contexts_table);
    new_context->n_samples = 1;
    list_inithead(&new_context->graphs);

    hw_context_update_process(ctx, new_context);

    new_context->current_graph_samples =
        get_accumulated_sample(ctx,
                               ctx->current_graph_samples->start_report.chunk,
                               ctx->current_graph_samples->start_report.header,
                               GPUTOP_OA_INVALID_CTX_ID);

    _mesa_hash_table_insert(ctx->hw_contexts_table, uint_key(hw_id), new_context);

    list_addtail(&new_context->link, &ctx->hw_contexts);

    return new_context;
}

static void
put_hw_context(struct gputop_client_context *ctx, struct gputop_hw_context *old_context)
{
    if (!old_context || --old_context->n_samples)
        return;

    struct hash_entry *entry =
        _mesa_hash_table_search(ctx->hw_contexts_table, uint_key(old_context->hw_id));
    _mesa_hash_table_remove(ctx->hw_contexts_table, entry);

    list_for_each_entry_safe(struct gputop_accumulated_samples, samples,
                             &old_context->graphs, link) {
        put_accumulated_sample(ctx, samples);
    }
    if (old_context->current_graph_samples)
        put_accumulated_sample(ctx, old_context->current_graph_samples);

    list_del(&old_context->link);
    free(old_context);

    uint32_t i = 0;
    list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link)
        context->timeline_row = i++;
    assert(i == _mesa_hash_table_num_entries(ctx->hw_contexts_table));
}

static void
hw_context_add_time(struct gputop_hw_context *context,
                    struct gputop_accumulated_samples *samples, bool add)
{
    uint64_t delta = samples->timestamp_end - samples->timestamp_start;
    context->time_spent += add ? delta : -delta;
}

static void
hw_context_record_for_time(struct gputop_client_context *ctx,
                           struct gputop_hw_context *context,
                           struct gputop_i915_perf_chunk *chunk,
                           const struct drm_i915_perf_record_header *header)
{
    struct gputop_accumulated_samples *samples = context->current_graph_samples;
    assert(context->current_graph_samples != NULL);
    context->current_graph_samples = NULL;

    samples->end_report.chunk = ref_i915_perf_chunk(chunk);
    samples->end_report.header = header;

    /* Put end timestamp */
    const uint64_t *cpu_timestamp = (const uint64_t *)
        gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                      GPUTOP_I915_PERF_FIELD_CPU_TIMESTAMP);
    samples->timestamp_end =
        cpu_timestamp ? (*cpu_timestamp) : samples->accumulator.last_timestamp;

    uint64_t usage_ns =
        gputop_timebase_scale_ns(&ctx->devinfo,
                                 samples->accumulator.clock.clock_count);
    context->usage_percent = (double) usage_ns / ctx->oa_aggregation_period_ns;

    /* Remove excess of samples */
    uint32_t max_graphs =
        (ctx->oa_visible_timeline_s * 1000000000.0f) / ctx->oa_aggregation_period_ns;
    while (context->n_graphs > max_graphs) {
        struct gputop_accumulated_samples *ex_samples =
            list_first_entry(&context->graphs, struct gputop_accumulated_samples, link);
        put_accumulated_sample(ctx, ex_samples);
        context->n_graphs--;
    }

    list_addtail(&samples->link, &context->graphs);
    context->n_graphs++;
}

static struct gputop_accumulated_samples *
get_accumulated_sample(struct gputop_client_context *ctx,
                       struct gputop_i915_perf_chunk *chunk,
                       const struct drm_i915_perf_record_header *header,
                       uint32_t hw_id)
{
    struct gputop_accumulated_samples *samples;

    if (list_empty(&ctx->free_samples)) {
        samples = (struct gputop_accumulated_samples *) calloc(1, sizeof(*samples));
    } else {
        samples = list_first_entry(&ctx->free_samples, struct gputop_accumulated_samples, link);
        list_del(&samples->link);
        memset(samples, 0, sizeof(*samples));
    }

    const uint8_t *report = (const uint8_t *)
        gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                      GPUTOP_I915_PERF_FIELD_OA_REPORT);
    gputop_cc_oa_accumulator_init(&samples->accumulator,
                                  &ctx->devinfo,
                                  ctx->metric_set,
                                  ctx->oa_aggregation_period_ns,
                                  report);

    list_inithead(&samples->link);
    samples->context = hw_id != GPUTOP_OA_INVALID_CTX_ID ? get_hw_context(ctx, hw_id) : NULL;
    samples->start_report.chunk = ref_i915_perf_chunk(chunk);
    samples->start_report.header = header;

    samples->timestamp_start = i915_perf_timestamp(ctx, header);

    return samples;
}

static void
put_accumulated_sample(struct gputop_client_context *ctx,
                       struct gputop_accumulated_samples *samples)
{
    put_i915_perf_chunk(samples->start_report.chunk);
    put_i915_perf_chunk(samples->end_report.chunk);
    put_hw_context(ctx, samples->context);
    list_del(&samples->link);
    list_add(&samples->link, &ctx->free_samples);
}

void
gputop_accumulated_samples_print(struct gputop_client_context *ctx,
                                 struct gputop_accumulated_samples *samples)
{
    struct gputop_record_iterator iter;
    gputop_record_iterator_init(&iter, samples);

    const struct drm_i915_perf_record_header *last = NULL;
    while (gputop_record_iterator_next(&iter)) {
        if (iter.header->type != DRM_I915_PERF_RECORD_SAMPLE)
            continue;

        if (last) {
            const uint32_t *report =
                (const uint32_t *) gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                                                 iter.header,
                                                                 GPUTOP_I915_PERF_FIELD_OA_REPORT);

            struct gputop_cc_oa_accumulator acc;
            gputop_cc_oa_accumulator_init(&acc, &ctx->devinfo, ctx->metric_set, 0, NULL);
            gputop_cc_oa_accumulate_reports(&acc,
                                            gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                                                          last,
                                                                          GPUTOP_I915_PERF_FIELD_OA_REPORT),
                                            (const uint8_t *) report);

            switch (ctx->metric_set->perf_oa_format) {
            case I915_OA_FORMAT_A32u40_A4u32_B8_C8:
                fprintf(stderr, "TS=%lx %s\n", acc.deltas[0],
                        gputop_i915_perf_record_reason(&ctx->i915_perf_config,
                                                       &ctx->devinfo,
                                                       iter.header));
                fprintf(stderr, "CLK=%lx\n", acc.deltas[1]);

                /* /\* 32x 40bit A counters... *\/ */
                /* for (i = 0; i < 32; i++) */
                /*     fprintf(stderr, "A%i=%lx\n", i, acc.deltas[i + 2]); */

                /* /\* 4x 32bit A counters... *\/ */
                /* for (i = 0; i < 4; i++) */
                /*     fprintf(stderr, "A%i=%lx\n", i, acc.deltas[i + 2 + 32]); */

                /* /\* 8x 32bit B counters + 8x 32bit C counters... *\/ */
                /* for (i = 0; i < 16; i++) */
                /*     fprintf(stderr, "B/C%i=%lx\n", i, acc.deltas[i + 2 + 32 + 4]); */
                fprintf(stderr, "B/C%i=%x B/C%i=%x\n",
                        8, report[48],
                        9, report[49]);
                break;

            case I915_OA_FORMAT_A45_B8_C8:
                fprintf(stderr, "TS=%lx\n", acc.deltas[0]);

                /* for (i = 0; i < 61; i++) */
                /*     fprintf(stderr, "A%i=%lx\n", i, acc.deltas[i + 1]); */
                break;
            default:
                assert(0);
            }
        }

        last = iter.header;
    }
}

double
gputop_client_context_read_counter_value(struct gputop_client_context *ctx,
                                         struct gputop_accumulated_samples *sample,
                                         const struct gputop_metric_set_counter *counter)
{
    switch (counter->data_type) {
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT64:
    case GPUTOP_PERFQUERY_COUNTER_DATA_UINT32:
    case GPUTOP_PERFQUERY_COUNTER_DATA_BOOL32:
        return counter->oa_counter_read_uint64(&ctx->devinfo,
                                               ctx->metric_set,
                                               sample->accumulator.deltas);
        break;
    case GPUTOP_PERFQUERY_COUNTER_DATA_DOUBLE:
    case GPUTOP_PERFQUERY_COUNTER_DATA_FLOAT:
        return counter->oa_counter_read_float(&ctx->devinfo,
                                              ctx->metric_set,
                                              sample->accumulator.deltas);
        break;
    }

    return 0.0f;
}

static void
i915_perf_record_for_time(struct gputop_client_context *ctx,
                          struct gputop_i915_perf_chunk *chunk,
                          const struct drm_i915_perf_record_header *header)
{
    struct gputop_accumulated_samples *samples = ctx->current_graph_samples;
    ctx->current_graph_samples = NULL;

    samples->end_report.chunk = ref_i915_perf_chunk(chunk);
    samples->end_report.header = header;

    /* Put end timestamp */
    samples->timestamp_end = i915_perf_timestamp(ctx, header);

    /* Remove excess of samples */
    uint32_t max_graphs =
        (ctx->oa_visible_timeline_s * 1000000000.0f) / ctx->oa_aggregation_period_ns;
    while (ctx->n_graphs > max_graphs) {
        struct gputop_accumulated_samples *ex_samples =
            list_first_entry(&ctx->graphs, struct gputop_accumulated_samples, link);
        put_accumulated_sample(ctx, ex_samples);
        ctx->n_graphs--;
    }

    list_addtail(&samples->link, &ctx->graphs);
    ctx->n_graphs++;
}

static void
i915_perf_record_for_hw_id(struct gputop_client_context *ctx,
                           struct gputop_i915_perf_chunk *chunk,
                           const struct drm_i915_perf_record_header *header)
{
    struct gputop_accumulated_samples *samples = ctx->current_timeline_samples;
    ctx->current_timeline_samples = NULL;

    samples->end_report.chunk = ref_i915_perf_chunk(chunk);
    samples->end_report.header = header;

    /* Put end timestamp */
    samples->timestamp_end = i915_perf_timestamp(ctx, header);

    /* Remove excess of samples */
    uint64_t aggregation_period_ns = ctx->oa_visible_timeline_s * 1000000000UL;
    struct gputop_accumulated_samples *first_samples =
        list_first_entry(&ctx->timelines, struct gputop_accumulated_samples, link);
    while (!list_empty(&ctx->timelines) &&
           (samples->timestamp_end - first_samples->timestamp_start) > aggregation_period_ns) {
        hw_context_add_time(first_samples->context, first_samples, false);
        put_accumulated_sample(ctx, first_samples);
        ctx->n_timelines--;
        first_samples = list_first_entry(&ctx->timelines,
                                         struct gputop_accumulated_samples, link);
    }

    list_addtail(&samples->link, &ctx->timelines);
    ctx->n_timelines++;

    hw_context_add_time(samples->context, samples, true);
}

static void
i915_perf_accumulate(struct gputop_client_context *ctx,
                     struct gputop_i915_perf_chunk *chunk)
{
    const struct drm_i915_perf_record_header *header;
    const uint8_t *last = ctx->last_header ?
        ((const uint8_t *) gputop_i915_perf_record_field(&ctx->i915_perf_config,
                                                         ctx->last_header,
                                                         GPUTOP_I915_PERF_FIELD_OA_REPORT)) :
        NULL;

    for (header = (const struct drm_i915_perf_record_header *) chunk->data;
         (const uint8_t *) header < (chunk->data + chunk->length);
         header = (const struct drm_i915_perf_record_header *) (((const uint8_t *)header) + header->size))
    {
        switch (header->type) {
        case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
            gputop_cr_console_log("i915_oa: OA buffer error - all records lost");
            gputop_client_context_stop_sampling(ctx);
            return;
        case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
            if (ctx->warn_report_loss)
                gputop_cr_console_log("i915_oa: OA report lost");
            break;

        case DRM_I915_PERF_RECORD_SAMPLE: {
            const uint8_t *samples = (const uint8_t *)
                gputop_i915_perf_record_field(&ctx->i915_perf_config, header,
                                              GPUTOP_I915_PERF_FIELD_OA_REPORT);
            uint32_t hw_id = gputop_cc_oa_report_get_ctx_id(&ctx->devinfo, samples);

            if (!ctx->current_graph_samples) {
                /* Global accumulator */
                ctx->current_graph_samples =
		  get_accumulated_sample(ctx, chunk, header, GPUTOP_OA_INVALID_CTX_ID);
                /* Also store an accumulator per context, only accumulated on */
                list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
                    assert(context->current_graph_samples == NULL);
                    context->current_graph_samples =
		      get_accumulated_sample(ctx, chunk, header, GPUTOP_OA_INVALID_CTX_ID);
                }
            }
            if (last && ctx->last_hw_id != GPUTOP_OA_INVALID_CTX_ID &&
                !ctx->current_timeline_samples) {
                ctx->current_timeline_samples =
                    get_accumulated_sample(ctx, ctx->last_chunk, ctx->last_header,
                                           ctx->last_hw_id);
            }

            if (last) {
                struct gputop_cc_oa_accumulator *accumulator;

                if (ctx->current_timeline_samples) {
                    struct gputop_hw_context *context = ctx->current_timeline_samples->context;

                    /* Accumulate for the timeline on the currently running context. */
                    accumulator = &ctx->current_timeline_samples->accumulator;
                    if (gputop_cc_oa_accumulate_reports(accumulator, last, samples)) {
                        uint64_t elapsed =
                            accumulator->last_timestamp - accumulator->first_timestamp;

                        if (ctx->last_hw_id != hw_id ||
                            elapsed > (ctx->oa_aggregation_period_ns)) {
                            i915_perf_record_for_hw_id(ctx, chunk, header);
                        }
                    }

                    /* Accumulate for the running context over the
                     * accumulation period. */
                    accumulator = &context->current_graph_samples->accumulator;
                    gputop_cc_oa_accumulate_reports(accumulator, last, samples);
                }

                /* Accumulate globally over the accumulation period. */
                accumulator =
                    &ctx->current_graph_samples->accumulator;
                if (gputop_cc_oa_accumulate_reports(accumulator, last, samples)) {
                    uint64_t elapsed =
                        accumulator->last_timestamp - accumulator->first_timestamp;

                    if (elapsed > (ctx->oa_aggregation_period_ns)) {
                        i915_perf_record_for_time(ctx, chunk, header);
                        if (ctx->accumulate_cb)
                            ctx->accumulate_cb(ctx, NULL);
                        list_for_each_entry(struct gputop_hw_context, context, &ctx->hw_contexts, link) {
                            assert(context->current_graph_samples != NULL);
                            hw_context_record_for_time(ctx, context, chunk, header);
                            if (ctx->accumulate_cb)
                                ctx->accumulate_cb(ctx, context);
                        }
                    }
                }
            }

            ctx->last_oa_timestamp = i915_perf_timestamp(ctx, header);
            last = samples;
            ctx->last_hw_id = hw_id;
            ctx->last_header = header;
            if (ctx->last_chunk) put_i915_perf_chunk(ctx->last_chunk);
            ctx->last_chunk = ref_i915_perf_chunk(chunk);
            break;
        }

        default:
            gputop_cr_console_log("i915 perf: Spurious header type = %d", header->type);
            return;
        }
    }
}

/**/

const struct gputop_metric_set *
gputop_client_context_uuid_to_metric_set(struct gputop_client_context *ctx, const char *uuid)
{
    struct hash_entry *entry =
        _mesa_hash_table_search(ctx->gen_metrics->metric_sets_map, uuid);
    return entry ? ((struct gputop_metric_set *) entry->data) : NULL;
}

const struct gputop_metric_set *
gputop_client_context_symbol_to_metric_set(struct gputop_client_context *ctx,
                                           const char *symbol_name)
{
    list_for_each_entry(struct gputop_metric_set, metric_set,
                        &ctx->gen_metrics->metric_sets, link) {
        if (!strcmp(metric_set->symbol_name, symbol_name))
            return metric_set;
    }
    return NULL;
}

static void
build_equations_variables(struct gputop_devinfo *devinfo)
{
    struct gputop_devtopology *topology = &devinfo->topology;
    int subslice_stride = DIV_ROUND_UP(topology->max_eus_per_subslice, 8);
    int slice_stride = subslice_stride * topology->max_subslices;
    int subslice_slice_stride = DIV_ROUND_UP(topology->max_subslices, 8);

    devinfo->n_eus = 0;
    for (int s = 0; s < topology->max_slices; s++) {
        for (int ss = 0; ss < topology->max_subslices; ss++) {
            for (int eug = 0; eug < subslice_stride; eug++) {
                devinfo->n_eus +=
                    __builtin_popcount(topology->eus_mask[slice_stride * s +
                                                          subslice_stride * ss +
                                                          eug]);
            }
        }
    }

    devinfo->n_eu_slices = 0;
    for (int s = 0; s < DIV_ROUND_UP(topology->max_slices, 8); s++) {
        devinfo->n_eu_slices +=
            __builtin_popcount(topology->slices_mask[s]);
    }

    devinfo->n_eu_sub_slices = 0;
    for (int s = 0; s < topology->max_slices * subslice_slice_stride; s++) {
        devinfo->n_eu_sub_slices +=
            __builtin_popcount(topology->subslices_mask[s]);
    }

    devinfo->slice_mask = topology->slices_mask[0];

    /* Unfortunately the equations expect at $SubsliceMask variable were the
     * meaning of the bits varies from one platform to another. One could hope
     * that we get special operations to query slice/subslice availability
     * abstracting the storage of this information...
     */
    int subslice_bits_per_slice = 0;
    if (devinfo->gen <= 10) {
        /* Subslices are grouped by 3. */
        subslice_bits_per_slice = 3;
    } else if (devinfo->gen == 11) {
        /* Subslices are grouped by 8 */
        subslice_bits_per_slice = 8;
    } else {
        unreachable("Cannot build subslice mask for equations");
    }

    devinfo->subslice_mask = 0;
    for (int s = 0; s < topology->max_slices; s++) {
        for (int ss = 0; ss < MIN2(topology->max_subslices, 3); ss++) {
            bool enabled =
                (topology->subslices_mask[slice_stride * s + ss / 8] &
                 (1UL << (ss % 8))) != 0;
            if (enabled)
                devinfo->subslice_mask |= 1ULL << (subslice_bits_per_slice * s + ss);
        }
    }


    devinfo->eu_threads_count = devinfo->n_eus * topology->n_threads_per_eu;
}

static void
register_platform_metrics(struct gputop_client_context *ctx,
                          const Gputop__DevInfo *pb_devinfo)
{
    static const struct {
        const char *devname;
        struct gputop_gen * (*get_metrics_cb)(const struct gputop_devinfo *devinfo);
    } devname_to_metric_func[] = {
        { "hsw", gputop_oa_get_metrics_hsw },
        { "bdw", gputop_oa_get_metrics_bdw },
        { "chv", gputop_oa_get_metrics_chv },
        { "sklgt2", gputop_oa_get_metrics_sklgt2 },
        { "sklgt3", gputop_oa_get_metrics_sklgt3 },
        { "sklgt4", gputop_oa_get_metrics_sklgt4 },
        { "kblgt2", gputop_oa_get_metrics_kblgt2 },
        { "kblgt3", gputop_oa_get_metrics_kblgt3 },
        { "bxt", gputop_oa_get_metrics_bxt },
        { "glk", gputop_oa_get_metrics_glk },
        { "cflgt2", gputop_oa_get_metrics_cflgt2 },
        { "cflgt3", gputop_oa_get_metrics_cflgt3 },
        { "cnl", gputop_oa_get_metrics_cnl },
        { "icl", gputop_oa_get_metrics_icl },
        { "ehl", gputop_oa_get_metrics_lkf },
        { "tgl", gputop_oa_get_metrics_tgl },
    };

    struct gputop_devinfo *devinfo = &ctx->devinfo;
    snprintf(devinfo->devname, sizeof(devinfo->devname), "%s", pb_devinfo->devname);
    snprintf(devinfo->prettyname, sizeof(devinfo->prettyname), "%s", pb_devinfo->prettyname);
    devinfo->timestamp_frequency = pb_devinfo->timestamp_frequency;
    devinfo->devid = pb_devinfo->devid;
    devinfo->gen = pb_devinfo->gen;
    devinfo->gt_min_freq = pb_devinfo->gt_min_freq;
    devinfo->gt_max_freq = pb_devinfo->gt_max_freq;

    const Gputop__DevTopology *pb_topology = pb_devinfo->topology;
    struct gputop_devtopology *topology = &ctx->devinfo.topology;

    memset(topology, 0, sizeof(*topology));

    topology->max_slices = pb_topology->max_slices;
    topology->max_subslices = pb_topology->max_subslices;
    topology->max_eus_per_subslice = pb_topology->max_eus_per_subslice;
    topology->n_threads_per_eu = pb_topology->n_threads_per_eu;

    assert(pb_topology->slices_mask.len <= ARRAY_SIZE(topology->slices_mask));
    for (uint32_t i = 0; i < pb_topology->slices_mask.len; i++)
        topology->slices_mask[i] = pb_topology->slices_mask.data[i];

    assert(pb_topology->subslices_mask.len <= ARRAY_SIZE(topology->subslices_mask));
    memcpy(topology->subslices_mask, pb_topology->subslices_mask.data,
           pb_topology->subslices_mask.len);

    assert(pb_topology->eus_mask.len <= ARRAY_SIZE(topology->eus_mask));
    memcpy(topology->eus_mask, pb_topology->eus_mask.data,
           pb_topology->eus_mask.len);

    build_equations_variables(devinfo);

    for (uint32_t i = 0; i < ARRAY_SIZE(devname_to_metric_func); i++) {
        if (!strcmp(devinfo->devname, devname_to_metric_func[i].devname)) {
            ctx->gen_metrics = devname_to_metric_func[i].get_metrics_cb(devinfo);
            return;
        }
    }
}

/**/

static void
close_i915_perf_stream(struct gputop_client_context *ctx)
{
    if (is_stream_opened(&ctx->oa_stream))
        close_stream(&ctx->oa_stream, ctx);
}

static void
open_i915_perf_stream(struct gputop_client_context *ctx)
{
    if (!ctx->metric_set) return;

    assert(!is_stream_opened(&ctx->oa_stream));

    i915_perf_empty_samples(ctx);

    if (ctx->oa_sampling_period_ns > ctx->oa_aggregation_period_ns) {
        ctx->oa_sampling_period_ns =
            gputop_oa_exponent_to_period_ns(
                &ctx->devinfo,
                gputop_time_to_oa_exponent(
                    &ctx->devinfo,
                    ctx->oa_aggregation_period_ns));
    }

    Gputop__OAStreamInfo oa_stream = GPUTOP__OASTREAM_INFO__INIT;
    oa_stream.uuid = (char *) ctx->metric_set->hw_config_guid;
    oa_stream.period_exponent =
        gputop_time_to_oa_exponent(&ctx->devinfo, ctx->oa_sampling_period_ns);
    oa_stream.per_ctx_mode = false;
    oa_stream.cpu_timestamps = ctx->i915_perf_config.cpu_timestamps;
    oa_stream.gpu_timestamps = ctx->i915_perf_config.gpu_timestamps;

    Gputop__OpenStream stream = GPUTOP__OPEN_STREAM__INIT;
    stream.overwrite = false;
    stream.live_updates = true;
    stream.type_case = GPUTOP__OPEN_STREAM__TYPE_OA_STREAM;
    stream.oa_stream = &oa_stream;

    open_stream(&ctx->oa_stream, ctx, &stream);
}

/**/

static void
open_perf_events_streams(struct gputop_client_context *ctx)
{
    // clear_perf_tracepoints_data(ctx);
    // list_for_each_entry(struct perf_event, tp, &ctx->perf_events, link)
    //     open_perf_event(ctx, tp);
}

static void
close_perf_events_streams(struct gputop_client_context *ctx)
{
    // list_for_each_entry(struct perf_event, tp, &ctx->perf_events, link)
    //     close_perf_event(ctx, tp);
}

/**/

static void
open_perf_tracepoints_streams(struct gputop_client_context *ctx)
{
    clear_perf_tracepoints_data(ctx);
    list_for_each_entry(struct gputop_perf_tracepoint, tp, &ctx->perf_tracepoints, link) {
        assert(list_length(&tp->data) == 0);
        open_perf_tracepoint(ctx, tp);
    }
}

static void
close_perf_tracepoints_streams(struct gputop_client_context *ctx)
{
    list_for_each_entry(struct gputop_perf_tracepoint, tp, &ctx->perf_tracepoints, link)
        close_perf_tracepoint(ctx, tp);
}

/**/

static void
request_features(struct gputop_client_context *ctx)
{
    Gputop__Request request = GPUTOP__REQUEST__INIT;
    request.req_case = GPUTOP__REQUEST__REQ_GET_FEATURES;
    request.get_features = true;

    send_pb_message(ctx, &request.base);
}

/**/

static bool
add_cpu_stats(struct gputop_client_context *ctx, Gputop__Message *message)
{
    if (!is_stream_opened(&ctx->cpu_stats_stream) ||
        message->cpu_stats->id != ctx->cpu_stats_stream.id)
        return false;

    uint32_t max_cpu_stats =
        (ctx->cpu_stats_visible_timeline_s * 1000.0f) / ctx->cpu_stats_sampling_period_ms;
    struct gputop_cpu_stat *stat;

    /* Remove excess of samples */
    while (ctx->n_cpu_stats > max_cpu_stats) {
        stat = list_first_entry(&ctx->cpu_stats, struct gputop_cpu_stat, link);
        list_del(&stat->link);
        gputop__message__free_unpacked(stat->stat, NULL);
        free(stat);
        ctx->n_cpu_stats--;
    }

    if (ctx->n_cpu_stats < max_cpu_stats) {
        stat = (struct gputop_cpu_stat *) calloc(1, sizeof(*stat));
        ctx->n_cpu_stats++;
    } else {
        stat = list_first_entry(&ctx->cpu_stats, struct gputop_cpu_stat, link);
        list_del(&stat->link);
        gputop__message__free_unpacked(stat->stat, NULL);
    }

    stat->stat = message;
    list_addtail(&stat->link, &ctx->cpu_stats);

    return true;
}

static void
open_cpu_stats_stream(struct gputop_client_context *ctx)
{
    /**/
    list_for_each_entry_safe(struct gputop_cpu_stat, stat, &ctx->cpu_stats, link) {
        list_del(&stat->link);
        gputop__message__free_unpacked(stat->stat, NULL);
        free(stat);
    }
    ctx->n_cpu_stats = 0;

    Gputop__CpuStatsInfo cpu_stats = GPUTOP__CPU_STATS_INFO__INIT;
    cpu_stats.sample_period_ms = ctx->cpu_stats_sampling_period_ms;

    Gputop__OpenStream stream = GPUTOP__OPEN_STREAM__INIT;
    stream.overwrite = false;
    stream.live_updates = true;
    stream.type_case = GPUTOP__OPEN_STREAM__TYPE_CPU_STATS;
    stream.cpu_stats = &cpu_stats;

    open_stream(&ctx->cpu_stats_stream, ctx, &stream);
}


void gputop_client_context_update_cpu_stream(struct gputop_client_context *ctx,
                                             int sampling_period_ms)
{
    if (is_stream_opened(&ctx->cpu_stats_stream))
        close_stream(&ctx->cpu_stats_stream, ctx);
    ctx->cpu_stats_sampling_period_ms = sampling_period_ms;
    open_cpu_stats_stream(ctx);
}

/**/

void
gputop_client_context_stop_sampling(struct gputop_client_context *ctx)
{
    if (!ctx->is_sampling)
        return;

    close_i915_perf_stream(ctx);
    close_perf_events_streams(ctx);
    close_perf_tracepoints_streams(ctx);

    ctx->is_sampling = false;
}

void
gputop_client_context_start_sampling(struct gputop_client_context *ctx)
{
    if (ctx->is_sampling)
        gputop_client_context_stop_sampling(ctx);

    _mesa_hash_table_clear(ctx->pid_to_process_table, delete_process_entry);
    _mesa_hash_table_clear(ctx->hw_id_to_process_table, NULL);

    open_i915_perf_stream(ctx);
    open_perf_events_streams(ctx);
    open_perf_tracepoints_streams(ctx);

    ctx->is_sampling = true;
}


/**/

static void
handle_perf_data(struct gputop_client_context *ctx,
                 uint32_t stream_id, const uint8_t *data, size_t len)
{
    struct hash_entry *entry =
        _mesa_hash_table_search(ctx->perf_tracepoints_stream_table,
                                uint_key(stream_id));
    if (!entry) {
        gputop_cr_console_log("Unknown stream id=%u\n", stream_id);
        return;
    }

    struct gputop_perf_tracepoint_stream *stream =
        (struct gputop_perf_tracepoint_stream *) entry->data;
    const uint8_t *data_end = data + len;

    while (data < data_end) {
        const struct gputop_perf_data_tracepoint *point =
            (const struct gputop_perf_data_tracepoint *) data;
        add_tracepoint_stream_data(ctx, stream, data, point->header.size);
        data += point->header.size;
    }
}

static void
handle_i915_perf_data(struct gputop_client_context *ctx,
                      uint32_t stream_id, const uint8_t *data, size_t len)
{
    if (stream_id == ctx->oa_stream.id) {
        struct gputop_i915_perf_chunk *chunk = get_i915_perf_chunk(ctx, data, len);
        i915_perf_accumulate(ctx, chunk);
        put_i915_perf_chunk(chunk);
    } else
        gputop_cr_console_log("discard wrong oa stream id=%i/%i",
                              stream_id, ctx->oa_stream.id);
}

static void
log_add(struct gputop_client_context *ctx, int level, const char *msg)
{
    if (ctx->n_messages < ARRAY_SIZE(ctx->messages)) {
        ctx->messages[ctx->n_messages].level = level;
        ctx->messages[ctx->n_messages].msg = strdup(msg);
        ctx->n_messages++;
    } else {
        int idx = (++ctx->start_message + ctx->n_messages) % ARRAY_SIZE(ctx->messages);

        free(ctx->messages[idx].msg);

        ctx->messages[idx].level = level;
        ctx->messages[idx].msg = strdup(msg);
    }
}

static void
handle_protobuf_message(struct gputop_client_context *ctx,
                        const uint8_t *data, size_t len)
{
    Gputop__Message *message =
        (Gputop__Message *) protobuf_c_message_unpack(&gputop__message__descriptor,
                                                      NULL, /* default allocator */
                                                      len, data);

    if (!message) {
        gputop_cr_console_log("Failed to unpack message len=%u", len);
        return;
    }

    switch (message->cmd_case) {
    case GPUTOP__MESSAGE__CMD_ERROR:
        log_add(ctx, 0, message->error);
        break;
    case GPUTOP__MESSAGE__CMD_ACK:
        //gputop_cr_console_log("ack\n");
        break;
    case GPUTOP__MESSAGE__CMD_FEATURES:
        if (ctx->features)
            gputop__message__free_unpacked(ctx->features, NULL);
        ctx->features = message;
        register_platform_metrics(ctx, message->features->devinfo);
        ctx->i915_perf_config.cpu_timestamps =
            message->features->has_i915_oa_cpu_timestamps &&
          message->features->has_i915_oa_gpu_timestamps;
        ctx->i915_perf_config.gpu_timestamps =
          message->features->has_i915_oa_cpu_timestamps &&
            message->features->has_i915_oa_gpu_timestamps;
        message = NULL; /* Save that structure for internal use */
        break;
    case GPUTOP__MESSAGE__CMD_LOG:
        for (size_t i = 0; i < message->log->n_entries; i++) {
            log_add(ctx, message->log->entries[i]->log_level,
                    message->log->entries[i]->log_message);
        }
        break;
    case GPUTOP__MESSAGE__CMD_CLOSE_NOTIFY: {
        struct gputop_stream *stream = find_stream(ctx, message->close_notify->id);
        if (stream)
            gputop_cr_console_log("unexpected close notify id=%i",
                                  message->close_notify->id);
        break;
    }
    case GPUTOP__MESSAGE__CMD_FILL_NOTIFY: {
        struct gputop_stream *stream = find_stream(ctx, message->fill_notify->stream_id);
        if (stream)
            stream->fill = message->fill_notify->fill_percentage;
        break;
    }
    case GPUTOP__MESSAGE__CMD_PROCESS_INFO: {
        struct hash_entry *entry =
            _mesa_hash_table_search(ctx->pid_to_process_table,
                                    uint_key(message->process_info->pid));
        if (entry) {
            struct gputop_process_info *info = (struct gputop_process_info *) entry->data;
            snprintf(info->cmd, sizeof(info->cmd), "%s", message->process_info->comm);
            snprintf(info->cmd_line, sizeof(info->cmd_line), "%s", message->process_info->cmd_line);

            update_hw_contexts_process_info(ctx, info);
        }
        break;
    }
    case GPUTOP__MESSAGE__CMD_CPU_STATS:
        if (add_cpu_stats(ctx, message))
            message = NULL;
        break;
    case GPUTOP__MESSAGE__CMD_TRACEPOINT_INFO: {
        if (ctx->tracepoint_info)
            gputop__message__free_unpacked(ctx->tracepoint_info, NULL);
        ctx->tracepoint_info = message;

        struct hash_entry *entry =
            _mesa_hash_table_search(ctx->perf_tracepoints_uuid_table, message->reply_uuid);
        if (entry) {
            update_tracepoint((struct gputop_perf_tracepoint *) entry->data,
                              message->tracepoint_info);
        }
        message = NULL;
        break;
    }
    case GPUTOP__MESSAGE__CMD__NOT_SET:
        assert(0);
    }

    if (message)
        gputop__message__free_unpacked(message, NULL);
}

void gputop_client_context_handle_data(struct gputop_client_context *ctx,
                                       const void *payload, size_t payload_len)
{
    const uint8_t *msg_type = (const uint8_t *) payload;
    const uint8_t *data = (const uint8_t *) payload + 8;
    size_t len = payload_len - 8;

    switch (*msg_type) {
    case 1: {
        const uint32_t *stream_id =
            (const uint32_t *) ((const uint8_t *) payload + 4);
        handle_perf_data(ctx, *stream_id, data, len);
        break;
    }
    case 2:
        handle_protobuf_message(ctx, data, len);
        break;
    case 3: {
        const uint32_t *stream_id =
            (const uint32_t *) ((const uint8_t *) payload + 4);
        handle_i915_perf_data(ctx, *stream_id, data, len);
        break;
    }
    default:
        gputop_cr_console_log("unknown msg type=%hhi", *msg_type);
        break;
    }
}

static void
i915_perf_empty_samples(struct gputop_client_context *ctx)
{
    list_for_each_entry_safe(struct gputop_accumulated_samples, samples,
                             &ctx->timelines, link) {
        put_accumulated_sample(ctx, samples);
    }
    if (ctx->current_timeline_samples) {
        put_accumulated_sample(ctx, ctx->current_timeline_samples);
        ctx->current_timeline_samples = NULL;
    }
    _mesa_hash_table_clear(ctx->hw_contexts_table, NULL);

    ctx->n_timelines = 0;
    ctx->last_hw_id = GPUTOP_OA_INVALID_CTX_ID;

    list_for_each_entry_safe(struct gputop_accumulated_samples, samples,
                             &ctx->graphs, link) {
        put_accumulated_sample(ctx, samples);
    }
    if (ctx->current_graph_samples) {
        put_accumulated_sample(ctx, ctx->current_graph_samples);
        ctx->current_graph_samples = NULL;
    }
    assert(list_empty(&ctx->graphs));
    ctx->n_graphs = 0;

    if (ctx->last_chunk) {
        put_i915_perf_chunk(ctx->last_chunk);
        ctx->last_chunk = NULL;
    }
    ctx->last_header = NULL;

    /* Make sure to leave some room for the UI to present timelines sliding
     * from the right hand side of the timeline view.
     */
    ctx->last_oa_timestamp = ctx->oa_visible_timeline_s * 1000000000ULL;

    assert(list_empty(&ctx->i915_perf_chunks));
}

static void
delete_process_entry(struct hash_entry *entry)
{
    struct gputop_process_info *info = entry->data;

    list_del(&info->link);
    free(info);
}

static void
clear_perf_tracepoints_data(struct gputop_client_context *ctx)
{
    list_for_each_entry_safe(struct gputop_perf_tracepoint_data, data,
                             &ctx->perf_tracepoints_data, link) {
        list_del(&data->link);
        list_del(&data->tp_link);
        free(data);
    }
}

void
gputop_client_context_clear_logs(struct gputop_client_context *ctx)
{
    for (int i = 0; i < ctx->n_messages; i++)
        free(ctx->messages[i].msg);
    ctx->start_message = ctx->n_messages = 0;
}

void
gputop_client_context_init(struct gputop_client_context *ctx)
{
    list_inithead(&ctx->cpu_stats);
    ctx->cpu_stats_visible_timeline_s = 7.0f;
    ctx->cpu_stats_sampling_period_ms = 100;

    ctx->oa_visible_timeline_s = 7.0f;
    ctx->oa_aggregation_period_ns = 60000000ULL; /* 60ms */
    ctx->oa_sampling_period_ns = 1000000ULL; /* 1ms */

    list_inithead(&ctx->streams);

    ctx->hw_contexts_table =
        _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    _mesa_hash_table_set_deleted_key(ctx->hw_contexts_table, uint_key(UINT32_MAX));
    _mesa_hash_table_set_freed_key(ctx->hw_contexts_table, uint_key(UINT32_MAX - 1));
    list_inithead(&ctx->hw_contexts);

    list_inithead(&ctx->graphs);
    list_inithead(&ctx->timelines);
    list_inithead(&ctx->free_samples);
    list_inithead(&ctx->i915_perf_chunks);

    list_inithead(&ctx->perf_tracepoints);
    list_inithead(&ctx->perf_tracepoints_data);
    ctx->perf_tracepoints_name_table =
        _mesa_hash_table_create(NULL, _mesa_hash_string, _mesa_key_string_equal);
    ctx->perf_tracepoints_uuid_table =
        _mesa_hash_table_create(NULL, _mesa_hash_string, _mesa_key_string_equal);
    ctx->perf_tracepoints_stream_table =
        _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

    ctx->pid_to_process_table =
        _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    ctx->hw_id_to_process_table =
        _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    _mesa_hash_table_set_deleted_key(ctx->pid_to_process_table, uint_key(UINT32_MAX));
    _mesa_hash_table_set_freed_key(ctx->pid_to_process_table, uint_key(UINT32_MAX - 1));
    _mesa_hash_table_set_deleted_key(ctx->hw_id_to_process_table, uint_key(UINT32_MAX));
    _mesa_hash_table_set_freed_key(ctx->hw_id_to_process_table, uint_key(UINT32_MAX - 1));
    list_inithead(&ctx->process_infos);

    ctx->i915_perf_config.oa_reports = true;
}

void
gputop_client_context_reset(struct gputop_client_context *ctx,
                            gputop_connection_t *connection)
{
    if (is_stream_opened(&ctx->cpu_stats_stream))
        list_inithead(&ctx->streams); /* Nuclear option... */

    /**/
    i915_perf_empty_samples(ctx);
    clear_perf_tracepoints_data(ctx);
    assert(list_length(&ctx->perf_tracepoints_data) == 0);

    /**/
    if (ctx->features) {
        gputop__message__free_unpacked(ctx->features, NULL);
        ctx->features = NULL;
    }
    if (ctx->tracepoint_info) {
        gputop__message__free_unpacked(ctx->tracepoint_info, NULL);
        ctx->tracepoint_info = NULL;
    }

    ralloc_free(ctx->gen_metrics);
    ctx->gen_metrics = NULL;
    ctx->metric_set = NULL;

    assert(list_length(&ctx->hw_contexts) == 0);
    assert(list_length(&ctx->streams) == 0);

    ctx->selected_uuid = -1;

    _mesa_hash_table_clear(ctx->perf_tracepoints_name_table, NULL);
    _mesa_hash_table_clear(ctx->perf_tracepoints_uuid_table, NULL);
    list_for_each_entry_safe(struct gputop_perf_tracepoint, tp, &ctx->perf_tracepoints, link) {
        gputop_client_context_remove_tracepoint(ctx, tp);
    }
    assert(list_length(&ctx->perf_tracepoints) == 0);

    /**/
    _mesa_hash_table_clear(ctx->pid_to_process_table, delete_process_entry);
    _mesa_hash_table_clear(ctx->hw_id_to_process_table, NULL);

    gputop_client_context_clear_logs(ctx);

    ctx->stream_id = 1; /* 0 reserved for closed/invalid */

    ctx->connection = connection;
    if (connection) {
        request_features(ctx);
        open_cpu_stats_stream(ctx);
    }
}
