#ifndef INCLUDED_MESSAGE_H
#define INCLUDED_MESSAGE_H

/* Stop using auto_more and use the new improved handling instead! */
#define AUTO_MORE_PROMPT     0
#define AUTO_MORE_SKIP_ONE   1   /* Skip to next message */
#define AUTO_MORE_SKIP_BLOCK 2   /* Skip to next message boundary */
#define AUTO_MORE_SKIP_ALL   3   /* Skip to next player action */
extern int auto_more_state;


extern void msg_on_startup(void);
extern void msg_on_shutdown(void);
extern void msg_on_load(savefile_ptr file);
extern void msg_on_save(savefile_ptr file);


extern int  msg_count(void);

extern cptr msg_text(int age);
extern int  msg_plain_text(int age, char *buffer, int max);
extern int  msg_turn(int age);

extern void msg_add(cptr msg);
extern void cmsg_add(byte color, cptr msg);
extern void msg_boundary(void);
extern int  msg_display_len(cptr msg);
extern void msg_print(cptr msg);
extern void msg_line_clear(bool close_drop_down);
extern void msg_line_init(const rect_t *display_rect);
extern bool msg_line_contains(int row, int col);
extern bool msg_line_is_empty(void);
extern rect_t msg_line_rect(void);
extern void cmsg_print(byte color, cptr msg);
extern void msg_format(cptr fmt, ...);
extern void cmsg_format(byte color, cptr fmt, ...);

#define PROMPT_FORCE_CHOICE   0x01 /* ignore quick_messages */
#define PROMPT_CASE_SENSITIVE 0x02
#define PROMPT_NEW_LINE       0x04
#define PROMPT_ESCAPE_DEFAULT 0x08
#define PROMPT_DEFAULT (PROMPT_NEW_LINE | PROMPT_ESCAPE_DEFAULT)
extern char msg_prompt(cptr prompt, char keys[], int options);
extern char cmsg_prompt(byte color, cptr prompt, char keys[], int options);


#endif