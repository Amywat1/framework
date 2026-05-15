#include "domain/model/wash_program.h"

#include <string.h>

#include "shared/timeouts.h"

void wash_program_init(wash_program_t *wash_program, const char *program_id, const char *program_name)
{
    if (wash_program == 0)
    {
        return;
    }

    memset(wash_program, 0, sizeof(*wash_program));
    if (program_id != 0)
    {
        strncpy(wash_program->program_id, program_id, sizeof(wash_program->program_id) - 1);
    }
    if (program_name != 0)
    {
        strncpy(wash_program->program_name, program_name, sizeof(wash_program->program_name) - 1);
    }
    wash_program->enabled = true;
    wash_program->default_segment_timeout_ms = DEFAULT_SEGMENT_TIMEOUT_MS;
    wash_program->default_exit_timeout_ms = DEFAULT_EXIT_TIMEOUT_MS;
    wash_program->revision = 1;
}

int wash_program_add_segment(wash_program_t *wash_program, const wash_segment_t *wash_segment)
{
    int expected_sequence_no;

    if (wash_program == 0 || wash_segment == 0)
    {
        return -1;
    }
    if (wash_program->segment_count >= MAX_PROGRAM_SEGMENTS)
    {
        return -1;
    }
    expected_sequence_no = wash_program->segment_count + 1;
    if (wash_segment->sequence_no != expected_sequence_no)
    {
        return -1;
    }
    wash_program->segments[wash_program->segment_count++] = *wash_segment;
    return 0;
}

const wash_segment_t *wash_program_get_segment(const wash_program_t *wash_program, int index)
{
    if (wash_program == 0 || index < 0 || index >= wash_program->segment_count)
    {
        return 0;
    }
    return &wash_program->segments[index];
}
