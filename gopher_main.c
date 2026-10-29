/****************************************************************************
 * apps/netutils/gopher/gopher_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>
#include <nuttx/serial/tioctl.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GOPHER_DEFAULT_HOST       "138.3.244.113"
#define GOPHER_DEFAULT_PORT       70
#define GOPHER_DEFAULT_SELECTOR   "/"

#define GOPHER_LINE_MAX           256
#define GOPHER_MENU_MAX           64
#define GOPHER_LABEL_MAX          80
#define GOPHER_SELECTOR_MAX       128
#define GOPHER_HOST_MAX           64
#define GOPHER_HISTORY_MAX        8
#define GOPHER_VIEW_ROWS          128
#define GOPHER_VIEW_COLS          256
#define GOPHER_RECV_TIMEOUT_SEC   8
#define GOPHER_RECV_BUFFER_SIZE   256

#define SCREEN_MIN_COLS           40
#define SCREEN_MAX_COLS           160
#define SCREEN_MIN_ROWS           12
#define SCREEN_MAX_ROWS           60
#define SCREEN_DEFAULT_COLS       80
#define SCREEN_DEFAULT_ROWS       24
#define BODY_FIRST_ROW            4

enum view_mode_e
{
  VIEW_AUTO,
  VIEW_80X24,
  VIEW_100X30,
  VIEW_120X40
};

enum gopher_row_type_e
{
  GOPHER_ROW_TEXT,
  GOPHER_ROW_ITEM
};

enum gopher_key_e
{
  KEY_UP = 1001,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_HOME,
  KEY_END,
  KEY_BACKTAB
};

struct gopher_reader_s
{
  unsigned char data[GOPHER_RECV_BUFFER_SIZE];
  size_t offset;
  size_t length;
};

struct gopher_item_s
{
  char type;
  char label[GOPHER_LABEL_MAX];
  char selector[GOPHER_SELECTOR_MAX];
  char host[GOPHER_HOST_MAX];
  unsigned short port;
};

struct gopher_location_s
{
  char host[GOPHER_HOST_MAX];
  char selector[GOPHER_SELECTOR_MAX];
  unsigned short port;
};

struct gopher_view_row_s
{
  enum gopher_row_type_e type;
  unsigned char item_index;
  char text[GOPHER_VIEW_COLS];
};

static struct gopher_item_s g_items[GOPHER_MENU_MAX];
static struct gopher_view_row_s g_rows[GOPHER_VIEW_ROWS];
static struct gopher_location_s g_history[GOPHER_HISTORY_MAX];
static unsigned int g_item_count;
static unsigned int g_row_count;
static unsigned int g_history_count;
static struct termios g_saved_termios;
static bool g_termios_saved;
static bool g_key_pending;
static char g_pending_key;
static enum view_mode_e g_view_mode = VIEW_AUTO;
static unsigned int g_screen_cols = SCREEN_DEFAULT_COLS;
static unsigned int g_screen_rows = SCREEN_DEFAULT_ROWS;

static int read_key(void);

static unsigned int body_rows(void)
{
  return g_screen_rows - 6;
}

static unsigned int footer_border_row(void)
{
  return g_screen_rows - 2;
}

static unsigned int footer_help_row(void)
{
  return g_screen_rows - 1;
}

static unsigned int footer_status_row(void)
{
  return g_screen_rows;
}

static void clamp_screen(unsigned int *cols, unsigned int *rows)
{
  if (*cols < SCREEN_MIN_COLS) *cols = SCREEN_MIN_COLS;
  if (*cols > SCREEN_MAX_COLS) *cols = SCREEN_MAX_COLS;
  if (*rows < SCREEN_MIN_ROWS) *rows = SCREEN_MIN_ROWS;
  if (*rows > SCREEN_MAX_ROWS) *rows = SCREEN_MAX_ROWS;
}

static bool update_screen_size(void)
{
  unsigned int old_cols = g_screen_cols;
  unsigned int old_rows = g_screen_rows;
  unsigned int cols = SCREEN_DEFAULT_COLS;
  unsigned int rows = SCREEN_DEFAULT_ROWS;

  if (g_view_mode == VIEW_80X24)
    {
      cols = 80;
      rows = 24;
    }
  else if (g_view_mode == VIEW_100X30)
    {
      cols = 100;
      rows = 30;
    }
  else if (g_view_mode == VIEW_120X40)
    {
      cols = 120;
      rows = 40;
    }
  else
    {
      struct winsize ws;
      memset(&ws, 0, sizeof(ws));
      if (ioctl(STDIN_FILENO, TIOCGWINSZ, (uintptr_t)&ws) == 0 &&
          ws.ws_col != 0 && ws.ws_row != 0)
        {
          cols = ws.ws_col;
          rows = ws.ws_row;
        }
    }

  clamp_screen(&cols, &rows);
  g_screen_cols = cols;
  g_screen_rows = rows;
  return old_cols != cols || old_rows != rows;
}

static void restore_terminal(void)
{
  if (g_termios_saved)
    {
      tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
      g_termios_saved = false;
    }

  printf("\033[0m\033[?25h\r\n");
  fflush(stdout);
}

static void setup_terminal(void)
{
  struct termios raw;

  if (tcgetattr(STDIN_FILENO, &g_saved_termios) < 0)
    {
      return;
    }

  g_termios_saved = true;
  raw = g_saved_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~OPOST;
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
    {
      g_termios_saved = false;
    }
  else
    {
      printf("\033[?25l");
      fflush(stdout);
    }
}

static int copy_field(char *dst, size_t dstlen, const char *src,
                      size_t srclen)
{
  if (srclen >= dstlen)
    {
      srclen = dstlen - 1;
    }

  memcpy(dst, src, srclen);
  dst[srclen] = '\0';
  return 0;
}

static int parse_menu_item(const char *line, struct gopher_item_s *item)
{
  const char *tab1;
  const char *tab2;
  const char *tab3;
  char *endptr;
  long port;

  if (line[0] == '\0' || line[0] == '.')
    {
      return 0;
    }

  tab1 = strchr(line + 1, '\t');
  if (tab1 == NULL)
    {
      return 0;
    }

  tab2 = strchr(tab1 + 1, '\t');
  tab3 = tab2 == NULL ? NULL : strchr(tab2 + 1, '\t');
  if (tab2 == NULL || tab3 == NULL)
    {
      return 0;
    }

  copy_field(item->label, sizeof(item->label), line + 1,
             tab1 - line - 1);
  copy_field(item->selector, sizeof(item->selector), tab1 + 1,
             tab2 - tab1 - 1);
  copy_field(item->host, sizeof(item->host), tab2 + 1,
             tab3 - tab2 - 1);

  errno = 0;
  port = strtol(tab3 + 1, &endptr, 10);
  if (errno != 0 || endptr == tab3 + 1 || port < 1 || port > 65535)
    {
      return 0;
    }

  item->type = line[0];
  item->port = (unsigned short)port;
  return 1;
}

static int connect_gopher(const char *host, unsigned short port)
{
  struct addrinfo hints;
  struct addrinfo *result;
  struct addrinfo *ai;
  char service[6];
  int fd = -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service, sizeof(service), "%u", port);

  if (getaddrinfo(host, service, &hints, &result) != 0)
    {
      return -1;
    }

  for (ai = result; ai != NULL; ai = ai->ai_next)
    {
      fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
        {
          break;
        }

      if (fd >= 0)
        {
          close(fd);
          fd = -1;
        }
    }

  freeaddrinfo(result);
  return fd;
}

static ssize_t send_all(int fd, const char *buffer, size_t length)
{
  size_t sent = 0;

  while (sent < length)
    {
      ssize_t ret = send(fd, buffer + sent, length - sent, 0);
      if (ret <= 0)
        {
          return -1;
        }

      sent += ret;
    }

  return (ssize_t)sent;
}

static int read_line(int fd, struct gopher_reader_s *reader, char *line,
                     size_t capacity, size_t *received)
{
  size_t used = 0;

  while (used + 1 < capacity)
    {
      unsigned char ch;

      if (reader->offset >= reader->length)
        {
          ssize_t ret = recv(fd, reader->data, sizeof(reader->data), 0);
          if (ret == 0)
            {
              line[used] = '\0';
              return used == 0 ? -2 : (int)used;
            }

          if (ret < 0)
            {
              return -1;
            }

          reader->offset = 0;
          reader->length = (size_t)ret;
          *received += (size_t)ret;
        }

      ch = reader->data[reader->offset++];
      if (ch == '\n')
        {
          break;
        }

      if (ch != '\r')
        {
          line[used++] = (char)ch;
        }
    }

  line[used] = '\0';
  return (int)used;
}

static size_t utf8_length(const unsigned char *text)
{
  unsigned char c = text[0];

  if (c < 0x80)
    {
      return 1;
    }

  if (c >= 0xc2 && c <= 0xdf && (text[1] & 0xc0) == 0x80)
    {
      return 2;
    }

  if (c >= 0xe0 && c <= 0xef && text[1] != '\0' && text[2] != '\0' &&
      (text[1] & 0xc0) == 0x80 && (text[2] & 0xc0) == 0x80)
    {
      return 3;
    }

  if (c >= 0xf0 && c <= 0xf4 && text[1] != '\0' && text[2] != '\0' &&
      text[3] != '\0' && (text[1] & 0xc0) == 0x80 &&
      (text[2] & 0xc0) == 0x80 && (text[3] & 0xc0) == 0x80)
    {
      return 4;
    }

  return 1;
}

static void add_view_text(const char *text)
{
  const unsigned char *cursor = (const unsigned char *)text;

  if (*cursor == '\0')
    {
      if (g_row_count < GOPHER_VIEW_ROWS)
        {
          g_rows[g_row_count].type = GOPHER_ROW_TEXT;
          g_rows[g_row_count].text[0] = '\0';
          g_row_count++;
        }

      return;
    }

  while (*cursor != '\0' && g_row_count < GOPHER_VIEW_ROWS)
    {
      size_t bytes = 0;
      size_t columns = 0;

      while (cursor[bytes] != '\0' && columns < g_screen_cols - 4)
        {
          size_t charlen = utf8_length(cursor + bytes);
          if (bytes + charlen >= sizeof(g_rows[0].text))
            {
              break;
            }

          bytes += charlen;
          columns++;
        }

      if (bytes == 0)
        {
          break;
        }

      g_rows[g_row_count].type = GOPHER_ROW_TEXT;
      copy_field(g_rows[g_row_count].text,
                 sizeof(g_rows[g_row_count].text), (const char *)cursor,
                 bytes);
      g_row_count++;
      cursor += bytes;
    }
}

static void add_item_row(unsigned int item_index)
{
  struct gopher_view_row_s *row;

  if (g_row_count >= GOPHER_VIEW_ROWS)
    {
      return;
    }

  row = &g_rows[g_row_count++];
  row->type = GOPHER_ROW_ITEM;
  row->item_index = item_index;
  snprintf(row->text, sizeof(row->text), "%2u  %s", item_index + 1,
           g_items[item_index].label);
}

static int fetch_page(const struct gopher_location_s *location,
                      char *message, size_t message_size)
{
  struct gopher_reader_s reader = {0};
  char line[GOPHER_LINE_MAX];
  char request[GOPHER_SELECTOR_MAX + 4];
  struct timeval recv_timeout;
  size_t received = 0;
  int fd;
  int length;
  bool is_menu = false;

  g_item_count = 0;
  g_row_count = 0;
  message[0] = '\0';

  fd = connect_gopher(location->host, location->port);
  if (fd < 0)
    {
      snprintf(message, message_size, "Nem erheto el: %.50s:%u",
               location->host, location->port);
      return -1;
    }

  recv_timeout.tv_sec = GOPHER_RECV_TIMEOUT_SEC;
  recv_timeout.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
             sizeof(recv_timeout));

  if (snprintf(request, sizeof(request), "%s\r\n", location->selector) >=
      (int)sizeof(request) || send_all(fd, request, strlen(request)) < 0)
    {
      close(fd);
      snprintf(message, message_size, "Nem sikerult a keres leadasa.");
      return -1;
    }

  for (;;)
    {
      struct gopher_item_s item;

      length = read_line(fd, &reader, line, sizeof(line), &received);
      if (length == -2)
        {
          break;
        }

      if (length < 0)
        {
          snprintf(message, message_size,
                   errno == EAGAIN || errno == EWOULDBLOCK ?
                   "Idotullepes; a lap reszlegesen erkezett meg." :
                   "Hiba az oldal olvasasakor.");
          break;
        }

      if (length == 1 && line[0] == '.')
        {
          break;
        }

      if (line[0] == '.' && line[1] == '.')
        {
          memmove(line, line + 1, strlen(line));
        }

      if (line[0] == 'i' && strchr(line + 1, '\t') != NULL)
        {
          char *fields = strchr(line + 1, '\t');
          *fields = '\0';
          add_view_text(line + 1);
          continue;
        }

      if (parse_menu_item(line, &item))
        {
          is_menu = true;
          if (g_item_count < GOPHER_MENU_MAX &&
              (item.type == '0' || item.type == '1' || item.type == '7'))
            {
              g_items[g_item_count] = item;
              add_item_row(g_item_count);
              g_item_count++;
            }
        }
      else
        {
          add_view_text(line);
        }
    }

  close(fd);
  return is_menu ? 1 : 0;
}

static void make_url(const struct gopher_location_s *location,
                     char *url, size_t size)
{
  const char *selector = location->selector;

  if (selector[0] == '/')
    {
      snprintf(url, size, "gopher://%.50s:%u%.54s", location->host,
               location->port, selector);
    }
  else
    {
      snprintf(url, size, "gopher://%.50s:%u/%.53s", location->host,
               location->port, selector);
    }
}

static void draw_border(void)
{
  unsigned int i;
  putchar('+');
  for (i = 0; i < g_screen_cols - 2; i++)
    {
      putchar('-');
    }
  putchar('+');
}

static void draw_padded_text(const char *text, size_t columns)
{
  const unsigned char *cursor = (const unsigned char *)text;
  size_t used = 0;

  while (*cursor != '\0' && used < columns)
    {
      size_t charlen = utf8_length(cursor);
      unsigned char c = *cursor;

      if (charlen == 1 && (c < 0x20 || c == 0x7f))
        {
          putchar(' ');
        }
      else
        {
          fwrite(cursor, 1, charlen, stdout);
        }

      cursor += charlen;
      used++;
    }

  while (used++ < columns)
    {
      putchar(' ');
    }
}

static void draw_framed_text(const char *text, bool selected)
{
  printf("| ");
  if (selected)
    {
      printf("\033[7m");
    }

  draw_padded_text(text, g_screen_cols - 4);

  if (selected)
    {
      printf("\033[0m");
    }

  printf(" |");
}

static int find_item_row(unsigned int item_index)
{
  unsigned int i;
  for (i = 0; i < g_row_count; i++)
    {
      if (g_rows[i].type == GOPHER_ROW_ITEM &&
          g_rows[i].item_index == item_index)
        {
          return (int)i;
        }
    }

  return -1;
}

static void keep_selection_visible(unsigned int selected,
                                   unsigned int *top_row)
{
  int row = find_item_row(selected);
  if (row >= 0)
    {
      if ((unsigned int)row < *top_row)
        {
          *top_row = (unsigned int)row;
        }
      else if ((unsigned int)row >= *top_row + body_rows())
        {
          *top_row = (unsigned int)row - body_rows() + 1;
        }
    }
}

static void draw_screen(const struct gopher_location_s *location,
                        bool is_menu, unsigned int top_row,
                        int selected_item, const char *message)
{
  char url[120];
  unsigned int row;
  unsigned int page_count;
  unsigned int page_number;

  make_url(location, url, sizeof(url));
  page_count = (g_row_count + body_rows() - 1) / body_rows();
  if (page_count == 0)
    {
      page_count = 1;
    }
  page_number = top_row / body_rows() + 1;

  printf("\033[2J\033[H\033[0m");
  draw_border();
  printf("\033[2;1H");
  draw_framed_text(url, false);
  printf("\033[3;1H");
  draw_border();

  for (row = 0; row < body_rows(); row++)
    {
      unsigned int view_index = top_row + row;
      bool selected = false;

      if (view_index < g_row_count)
        {
          const char *text = g_rows[view_index].text;
          if (g_rows[view_index].type == GOPHER_ROW_ITEM &&
              g_rows[view_index].item_index == (unsigned int)selected_item)
            {
              selected = true;
            }

          printf("\033[%u;1H", BODY_FIRST_ROW + row);
          draw_framed_text(text, selected);
        }
      else
        {
          printf("\033[%u;1H", BODY_FIRST_ROW + row);
          draw_framed_text("", false);
        }
    }

  printf("\033[%u;1H", footer_border_row());
  draw_border();
  printf("\033[%u;1H", footer_help_row());
  if (is_menu)
    {
      draw_framed_text("Fel/le: valasztas  Tab: kovetkezo  Enter/jobbra: megnyit  v: nezet",
                       false);
    }
  else
    {
      draw_framed_text("Balra/b: vissza  q: kilepes  PgUp/PgDn: lapozas  v: nezet",
                       false);
    }

  {
    char footer[SCREEN_MAX_COLS];
    snprintf(footer, sizeof(footer),
             "%.6s | %u/%u | %.96s",
             is_menu ? "Menu" : "Szoveg", page_number, page_count,
             message == NULL ? "" : message);
    printf("\033[%u;1H", footer_status_row());
    draw_framed_text(footer, false);
    fflush(stdout);
  }
}

static bool choose_view_mode(const struct gopher_location_s *location)
{
  static const char *labels[] =
  {
    "Automatikus (Telnet meret, ha elerheto)",
    "80 x 24",
    "100 x 30",
    "120 x 40"
  };
  unsigned int selected = (unsigned int)g_view_mode;

  for (;;)
    {
      unsigned int i;
      int key;
      char url[120];

      make_url(location, url, sizeof(url));
      printf("\033[2J\033[H\033[0m");
      draw_border();
      printf("\033[2;1H");
      draw_framed_text(url, false);
      printf("\033[3;1H");
      draw_border();
      printf("\033[4;1H");
      draw_framed_text("Helyi kijelzonezet — csak a kliens elrendezeset allitja",
                       false);
      for (i = 0; i < 4; i++)
        {
          char line[SCREEN_MAX_COLS];
          snprintf(line, sizeof(line), "%c %s%s", i == selected ? '>' : ' ',
                   labels[i], i == (unsigned int)g_view_mode ? "  [aktiv]" : "");
          printf("\033[%u;1H", 5 + i);
          draw_framed_text(line, i == selected);
        }

      printf("\033[%u;1H", footer_border_row());
      draw_border();
      printf("\033[%u;1H", footer_help_row());
      draw_framed_text("Fel/le: valasztas  Enter: alkalmaz  Esc/b: vissza",
                       false);
      printf("\033[%u;1H", footer_status_row());
      draw_framed_text("A fix meret csak erre a futasra ervenyes.", false);
      fflush(stdout);

      key = read_key();
      if (key < 0 || key == 0x1b || key == 'b' || key == 'B')
        {
          return false;
        }
      if (key == KEY_UP || key == KEY_BACKTAB)
        {
          selected = selected == 0 ? 3 : selected - 1;
        }
      else if (key == KEY_DOWN || key == '\t')
        {
          selected = (selected + 1) % 4;
        }
      else if (key == '\n' || key == KEY_RIGHT)
        {
          enum view_mode_e previous = g_view_mode;
          g_view_mode = (enum view_mode_e)selected;
          update_screen_size();
          return previous != g_view_mode || selected == VIEW_AUTO;
        }
    }
}

static int read_byte(char *ch)
{
  if (g_key_pending)
    {
      *ch = g_pending_key;
      g_key_pending = false;
      return 1;
    }

  for (;;)
    {
      ssize_t ret = read(STDIN_FILENO, ch, 1);
      if (ret == 1)
        {
          return 1;
        }

      if (ret == 0)
        {
          return -1;
        }

      if (ret < 0 && errno != EINTR)
        {
          return -1;
        }
    }
}

static int read_escape_byte(char *ch)
{
  fd_set readfds;
  struct timeval timeout;
  int ret;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;

  ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
  if (ret <= 0)
    {
      return 0;
    }

  return read(STDIN_FILENO, ch, 1) == 1 ? 1 : 0;
}

static int read_optional_byte(char *ch)
{
  return read_escape_byte(ch);
}

static int read_key(void)
{
  char ch;
  if (read_byte(&ch) < 0)
    {
      return -1;
    }

  if (ch == '\r')
    {
      char next;
      if (read_optional_byte(&next) && next != '\n' && next != '\0')
        {
          g_pending_key = next;
          g_key_pending = true;
        }

      return '\n';
    }

  if (ch == '\n') return '\n';
  if (ch == '\t') return '\t';
  if (ch == 0x7f || ch == 0x08) return 0x08;

  if (ch != 0x1b)
    {
      return (unsigned char)ch;
    }

  if (!read_escape_byte(&ch) || ch != '[')
    {
      return 0x1b;
    }

  if (!read_escape_byte(&ch)) return 0x1b;
  switch (ch)
    {
      case 'A': return KEY_UP;
      case 'B': return KEY_DOWN;
      case 'C': return KEY_RIGHT;
      case 'D': return KEY_LEFT;
      case 'H': return KEY_HOME;
      case 'F': return KEY_END;
      case 'Z': return KEY_BACKTAB;
      case '5':
        if (read_escape_byte(&ch) && ch == '~') return KEY_PAGE_UP;
        break;
      case '6':
        if (read_escape_byte(&ch) && ch == '~') return KEY_PAGE_DOWN;
        break;
      default:
        break;
    }

  return 0x1b;
}

static int read_search(char *query, size_t capacity)
{
  size_t used = 0;

  query[0] = '\0';
  for (;;)
    {
      char prompt[77];
      int key;

      snprintf(prompt, sizeof(prompt), "Kereses: %s", query);
      printf("\033[%u;1H", footer_status_row());
      draw_framed_text(prompt, false);
      fflush(stdout);

      key = read_key();
      if (key < 0 || key == 'q' || key == 0x1b)
        {
          return -1;
        }
      if (key == '\n')
        {
          return (int)used;
        }
      if (key == 0x08)
        {
          if (used > 0)
            {
              query[--used] = '\0';
            }
          continue;
        }
      if (key >= 0x20 && key <= 0x7e && used + 1 < capacity)
        {
          query[used++] = (char)key;
          query[used] = '\0';
        }
    }
}

static void go_back(struct gopher_location_s *current)
{
  if (g_history_count > 0)
    {
      *current = g_history[--g_history_count];
    }
}

static void select_item(const struct gopher_location_s *current,
                        unsigned int selected,
                        struct gopher_location_s *next)
{
  const struct gopher_item_s *item = &g_items[selected];

  memset(next, 0, sizeof(*next));
  if (item->host[0] == '\0' || strcmp(item->host, "null.host") == 0)
    {
      strlcpy(next->host, current->host, sizeof(next->host));
      next->port = item->port == 1 ? current->port : item->port;
    }
  else
    {
      strlcpy(next->host, item->host, sizeof(next->host));
      next->port = item->port;
    }

  if (item->type == '7')
    {
      char query[64];
      char prompt[77];
      int n;

      snprintf(prompt, sizeof(prompt), "Kereses: ");
      printf("\033[%u;1H", footer_status_row());
      draw_framed_text(prompt, false);
      fflush(stdout);

      if (read_search(query, sizeof(query)) < 0)
        {
          next->selector[0] = '\0';
          return;
        }

      n = snprintf(next->selector, sizeof(next->selector), "%s\t%s",
                   item->selector, query);
      if (n < 0 || n >= (int)sizeof(next->selector))
        {
          next->selector[0] = '\0';
          return;
        }
    }
  else
    {
      strlcpy(next->selector, item->selector, sizeof(next->selector));
    }
}

int main(int argc, FAR char *argv[])
{
  struct gopher_location_s current;
  char message[77];
  unsigned int top_row = 0;
  unsigned int selected = 0;
  bool is_menu = false;
  int page_result;

  if (argc > 2)
    {
      fprintf(stderr, "usage: gopher [selector]\n");
      return 2;
    }

  memset(&current, 0, sizeof(current));
  strlcpy(current.host, GOPHER_DEFAULT_HOST, sizeof(current.host));
  strlcpy(current.selector,
          argc == 2 ? argv[1] : GOPHER_DEFAULT_SELECTOR,
          sizeof(current.selector));
  current.port = GOPHER_DEFAULT_PORT;
  g_history_count = 0;
  setup_terminal();
  update_screen_size();
  atexit(restore_terminal);

  for (;;)
    {
      page_result = fetch_page(&current, message, sizeof(message));
      if (page_result < 0)
        {
          is_menu = false;
          g_row_count = 1;
          g_rows[0].type = GOPHER_ROW_TEXT;
          strlcpy(g_rows[0].text, message, sizeof(g_rows[0].text));
        }
      else
        {
          is_menu = page_result == 1;
          if (g_item_count == 0)
            {
              selected = 0;
            }
          else if (selected >= g_item_count)
            {
              selected = 0;
            }
        }

      top_row = 0;
      keep_selection_visible(selected, &top_row);

      {
        bool refresh_page = false;
      for (;;)
        {
          int key;

          if (g_view_mode == VIEW_AUTO && update_screen_size())
            {
              refresh_page = true;
              break;
            }

          draw_screen(&current, is_menu, top_row,
                      g_item_count == 0 ? -1 : (int)selected, message);
          key = read_key();
          if (key < 0 || key == 'q' || key == 'Q' || key == 0x03)
            {
              restore_terminal();
              return 0;
            }

          if (key == 'v' || key == 'V')
            {
              refresh_page = choose_view_mode(&current);
              if (refresh_page)
                {
                  break;
                }

              continue;
            }

          if (key == 'b' || key == 'B' || key == KEY_LEFT)
            {
              if (g_history_count > 0)
                {
                  go_back(&current);
                  break;
                }

              snprintf(message, sizeof(message), "Nincs elozo oldal.");
              continue;
            }

          if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN)
            {
              if (key == KEY_PAGE_UP)
                {
                  top_row = top_row > body_rows() ?
                            top_row - body_rows() : 0;
                }
              else if (top_row + body_rows() < g_row_count)
                {
                  top_row += body_rows();
                }

              continue;
            }

          if (!is_menu || g_item_count == 0)
            {
              continue;
            }

          if (key == KEY_UP || key == KEY_BACKTAB)
            {
              selected = selected == 0 ? g_item_count - 1 : selected - 1;
              keep_selection_visible(selected, &top_row);
              continue;
            }

          if (key == KEY_DOWN || key == '\t')
            {
              selected = (selected + 1) % g_item_count;
              keep_selection_visible(selected, &top_row);
              continue;
            }

          if (key == KEY_HOME)
            {
              selected = 0;
              top_row = 0;
              continue;
            }

          if (key == KEY_END)
            {
              selected = g_item_count - 1;
              keep_selection_visible(selected, &top_row);
              continue;
            }

          if (key == '\n' || key == KEY_RIGHT)
            {
              struct gopher_location_s next;
              select_item(&current, selected, &next);
              if (next.selector[0] == '\0')
                {
                  snprintf(message, sizeof(message), "Ures vagy tul hosszu keres.");
                  continue;
                }

              if (g_history_count < GOPHER_HISTORY_MAX)
                {
                  g_history[g_history_count++] = current;
                }
              else
                {
                  memmove(g_history, g_history + 1,
                          sizeof(g_history[0]) * (GOPHER_HISTORY_MAX - 1));
                  g_history[GOPHER_HISTORY_MAX - 1] = current;
                }

              current = next;
              selected = 0;
              break;
            }
        }

        if (refresh_page)
          {
            continue;
          }
      }
    }
}
