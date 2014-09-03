
#include <arch/types.h>
#include <arch/postman.h>
#include <arch/gpio.h>

#define GPU_INSTANT_MESSAGE 0x40000000


int16 (*arch_framebuffer)[480][640];

struct FrameBufferInfo {
	int32 p_width;	/* Physical Width */
	int32 p_height;   /* Physical Height */
	int32 v_width;	/* Virtual Width */
	int32 v_height;   /* Virtual Height */
	int32 pitch;	  /* Pitch */
	int32 depth;	  /* Bit Depth */
	int32 x_offset;   /* X offset */
	int32 y_offset;   /* Y offset */
	int16 (*frame_buffer)[480][640]; /* Framebuffer - Pointer */
	int32 frame_buffer_size; /* Framebuffer - Size */
};

void arch_gpu_drawPixel(unsigned int x, unsigned int y, int16 color) {
	(*arch_framebuffer)[y][x] =  color;
}

void arch_gpu_init()
{
	int error;

	struct FrameBufferInfo frame_buffer_info __attribute__ ((aligned (16)));
	frame_buffer_info.p_width = 1920;
	frame_buffer_info.p_height = 1080;
	frame_buffer_info.v_width = 640;
	frame_buffer_info.v_height = 480;
	frame_buffer_info.pitch = 0;
	frame_buffer_info.depth = 16;
	frame_buffer_info.x_offset = 0;
	frame_buffer_info.y_offset = 0;
	frame_buffer_info.frame_buffer = 0;
	frame_buffer_info.frame_buffer_size = 0;

	unsigned int message = ((unsigned int) &frame_buffer_info) + GPU_INSTANT_MESSAGE;
	arch_send_message(message, 1);
	error = arch_read_message(1);
	if (error)
		return;

	arch_framebuffer = frame_buffer_info.frame_buffer;
}
