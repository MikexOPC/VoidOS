#include "../../../../../kernel/console/video/service/console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {
    uint64_t                Before;
    KERNEL_CONSOLE_SERVICE  Service;
    uint64_t                After;
} fixture;
static KERNEL_VIDEO_CONSOLE view, saved_view;
static KERNEL_CONSOLE_SERVICE saved;
static unsigned int checks;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL service line %d, check %u\n", __LINE__, checks); exit(1); } } while (0)
#define EDIT(data) kernel_console_service_edit(&fixture.Service, data, sizeof(data) - 1, sizeof(data))
#define WRITE(data) kernel_console_service_write(&fixture.Service, data, sizeof(data) - 1, sizeof(data))

// ====== setup ======

static void setup(uint32_t columns, uint32_t rows)
{
    fixture.Before = fixture.After = 0xabcde1234567890ULL;
    CHECK(kernel_console_service_init(&fixture.Service, &view, columns, rows));
    CHECK(!fixture.Service.Failed && fixture.Service.Length == 0);
}

// ====== unchanged ======

static void unchanged(void)
{
    CHECK(!memcmp(&fixture.Service, &saved, sizeof(saved)));
    CHECK(!memcmp(&view, &saved_view, sizeof(view)));
    CHECK(fixture.Before == 0xabcde1234567890ULL && fixture.After == fixture.Before);
}

// ====== ownership_and_bounds ======

static void ownership_and_bounds(void)
{
    const char invalid[] = "A\xe8";
    setup(12, 4);
    CHECK(view.Cells[0] == '>' && view.Cells[1] == ' ');
    saved = fixture.Service; saved_view = view;
    CHECK(!kernel_console_service_init(NULL, &view, 12, 4));
    CHECK(!kernel_console_service_init(&fixture.Service, &view, 241, 4));
    CHECK(!kernel_console_service_init(&fixture.Service, &fixture.Service.Transcript, 12, 4));
    CHECK(!kernel_console_service_edit(&saved, "A", 1, 1));
    CHECK(!kernel_console_service_edit(&fixture.Service, NULL, 0, 0));
    CHECK(!kernel_console_service_edit(&fixture.Service, "A", 1, 0));
    CHECK(!kernel_console_service_edit(&fixture.Service, "A", 257, 257));
    CHECK(!kernel_console_service_edit(&fixture.Service, invalid, 2, 2));
    CHECK(!kernel_console_service_write(&fixture.Service, invalid, 2, 2));
    CHECK(!EDIT("\n")); CHECK(!WRITE("\b")); CHECK(!WRITE("\x1b[31m"));
    CHECK(!kernel_console_service_edit(&fixture.Service, (const char *)&fixture.Service, 1, 1));
    CHECK(!kernel_console_service_write(&fixture.Service, (const char *)&view, 1, 1));
    unchanged();
    CHECK(kernel_console_service_render_begin(&fixture.Service));
    saved = fixture.Service; saved_view = view;
    CHECK(!EDIT("A")); CHECK(!WRITE("A"));
    CHECK(!kernel_console_service_submit(&fixture.Service));
    CHECK(!kernel_console_service_render_begin(&fixture.Service));
    CHECK(!kernel_console_service_close(&fixture.Service));
    CHECK(!kernel_console_service_render_end(&fixture.Service, 2));
    unchanged();
    CHECK(kernel_console_service_render_end(&fixture.Service, 1));
    CHECK(!kernel_console_service_render_end(&fixture.Service, 1));
    CHECK(kernel_console_service_render_begin(&fixture.Service));
    CHECK(!kernel_console_service_render_end(&fixture.Service, 0));
    CHECK(fixture.Service.Failed && !fixture.Service.Busy);
    saved = fixture.Service; saved_view = view;
    CHECK(!EDIT("A")); CHECK(!WRITE("A"));
    CHECK(kernel_console_service_pending(&fixture.Service) == -1);
    CHECK(!kernel_console_service_close(&fixture.Service));
    unchanged();
}

// ====== interleaved_response ======

static void interleaved_response(void)
{
    const char italian[] = "c\xc3\xa8 \xe2\x82\xac";
    setup(16, 4);
    CHECK(kernel_console_service_edit(&fixture.Service, italian, sizeof(italian) - 1, sizeof(italian)));
    CHECK(fixture.Service.Length == 4 && view.Cells[3] == 0xe8 && view.Cells[5] == 0x20ac);
    CHECK(WRITE("risposta\n"));
    CHECK(view.Cells[0] == 'r' && view.Cells[16] == '>' && view.Cells[19] == 0xe8);
    CHECK(fixture.Service.Length == 4 && fixture.Service.Transcript.Cells[16] == ' ');
    CHECK(EDIT("c"));
    CHECK(view.Cells[18] == 'c' && view.Cells[19] == ' ' && view.Cells[21] == ' ');
    CHECK(EDIT(""));
    CHECK(view.Cells[16] == '>' && view.Cells[18] == ' ');
    CHECK(EDIT("help")); CHECK(kernel_console_service_submit(&fixture.Service));
    CHECK(fixture.Service.Length == 0 && fixture.Service.Transcript.Cells[18] == 'h');
    CHECK(view.Cells[32] == '>' && view.Cells[34] == ' ');
    CHECK(EDIT("prossima")); CHECK(WRITE("OK\n"));
    CHECK(view.Cells[32] == 'O' && view.Cells[48] == '>' && view.Cells[50] == 'p');
    CHECK(fixture.Service.Length == 8);
    CHECK(kernel_console_service_pending(&fixture.Service) == 1);
    for (uint32_t start = 0, index, scalar; start < 64;) {
        int result = kernel_console_peek(&view, start, &index, &scalar);
        if (!result) break;
        CHECK(result == 1 && kernel_console_ack(&view, index, scalar));
        start = index + 1;
    }
    CHECK(kernel_console_service_pending(&fixture.Service) == 0);
    CHECK(kernel_console_service_close(&fixture.Service));
    CHECK(fixture.Service.Closed && fixture.Service.Length == 0);
    saved = fixture.Service; saved_view = view;
    CHECK(!EDIT("A")); CHECK(!WRITE("A")); CHECK(!kernel_console_service_submit(&fixture.Service));
    CHECK(!kernel_console_service_render_begin(&fixture.Service));
    CHECK(!kernel_console_service_close(&fixture.Service)); unchanged();
}

// ====== limits_and_scroll ======

static void limits_and_scroll(void)
{
    char full[256];
    memset(full, 'X', sizeof(full));
    setup(3, 2);
    CHECK(kernel_console_service_edit(&fixture.Service, full, sizeof(full), sizeof(full)));
    CHECK(fixture.Service.Length == 256 && !fixture.Service.Failed);
    CHECK(kernel_console_service_submit(&fixture.Service));
    CHECK(fixture.Service.Length == 0 && view.Cells[3] == '>' && view.Cells[4] == ' ');
    for (uint32_t cycle = 0; cycle < 10000; ++cycle) {
        CHECK(EDIT("ab")); CHECK(WRITE("012345\n"));
        CHECK(fixture.Service.Length == 2 && fixture.Service.Line[0] == 'a');
        CHECK(EDIT("a")); CHECK(kernel_console_service_submit(&fixture.Service));
        CHECK(!fixture.Service.Busy && !fixture.Service.Failed);
    }
    setup(1, 1);
    CHECK(EDIT("A")); CHECK(WRITE("B\n"));
    CHECK(view.Cells[0] == 'A' && fixture.Service.Length == 1);
    setup(240, 67);
    CHECK(kernel_console_service_edit(&fixture.Service, full, sizeof(full), sizeof(full)));
    CHECK(view.Cells[241] == 'X');
    CHECK(WRITE("result\n")); CHECK(view.Cells[242] == 'X');
    CHECK(fixture.Before == fixture.After);
}

// ====== caret_updates ======

static void caret_updates(void)
{
    uint32_t index, scalar;
    setup(8, 2);
    CHECK(view.Caret == 2 && view.Cells[2] == 32);
    while (kernel_console_peek(&view, 0, &index, &scalar) == 1)
        CHECK(kernel_console_ack(&view, index, scalar));
    CHECK(EDIT("a"));
    CHECK(view.Caret == 3 && view.Cells[2] == 'a');
    CHECK(view.Dirty[0] & (1U << 2)); CHECK(view.Dirty[0] & (1U << 3));
    CHECK(EDIT("")); CHECK(view.Caret == 2 && view.Cells[2] == 32);
    CHECK(EDIT("abcdef")); CHECK(view.X == 8 && view.Caret == 7 && view.Cells[7] == 'f');
    CHECK(EDIT("abcdefg")); CHECK(view.Caret == 9 && view.Cells[8] == 'g');
    CHECK(WRITE("response\n")); CHECK(view.Caret < 16);
    saved_view = view;
    CHECK(!kernel_console_caret(&view, 2)); CHECK(!memcmp(&view, &saved_view, sizeof(view)));
    CHECK(kernel_console_service_render_begin(&fixture.Service));
    CHECK(!EDIT("x")); CHECK(kernel_console_service_render_end(&fixture.Service, 1));
    CHECK(kernel_console_caret(&view, 0)); CHECK(view.Caret == UINT32_MAX);
    CHECK(kernel_console_caret(&view, 1)); CHECK(view.Caret < 16);
    CHECK(kernel_console_clear(&view)); CHECK(view.Caret == 0);
    view.Caret = view.Columns * view.Rows;
    saved_view = view; saved = fixture.Service;
    CHECK(!EDIT("x") && !WRITE("x")); unchanged();
    setup(8, 2); fixture.Service.Transcript.Caret = 0;
    saved_view = view; saved = fixture.Service;
    CHECK(!EDIT("x") && !WRITE("x")); unchanged();
}

// ====== main ======

// ====== clip_transcript ======

// Geometry A: after 50 lines on a 142x44 grid the clip to 113x42 keeps the prompt row, the
// transcript and the view agree, and the service keeps accepting edits and output.
static void clip_transcript(void)
{
    setup(142, 44);
    for (uint32_t line = 0; line < 50; ++line) CHECK(WRITE("riga\n"));
    CHECK(EDIT("help"));
    uint32_t y = view.Y;
    // A refused clip (wider than the grid) fails the service, as every refused service operation.
    CHECK(!kernel_console_service_clip(&fixture.Service, 143, 42) && fixture.Service.Failed);
    setup(142, 44);
    for (uint32_t line = 0; line < 50; ++line) CHECK(WRITE("riga\n"));
    CHECK(EDIT("help"));
    CHECK(kernel_console_service_clip(&fixture.Service, 113, 42));
    CHECK(view.Columns == 113 && view.Rows == 42 && fixture.Service.Transcript.Columns == 113);
    CHECK(fixture.Service.Transcript.Rows == 42 && view.Y == y - 2 && view.Caret != UINT32_MAX);
    CHECK(view.Cells[view.Y * 113] == '>' && view.Cells[view.Y * 113 + 2] == 'h');
    CHECK(kernel_console_service_pending(&fixture.Service) == 1);
    CHECK(EDIT("helpx") && WRITE("ok\n") && kernel_console_service_pending(&fixture.Service) == 1);
    CHECK(fixture.Before == fixture.After);
}

int main(void)
{
    ownership_and_bounds(); interleaved_response(); limits_and_scroll(); caret_updates(); clip_transcript();
    printf("PASS console service: %u checks; owner %zu bytes; fixed storage, no allocations\n",
        checks, sizeof(fixture.Service));
    return 0;
}
