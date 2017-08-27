#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <dbus/dbus.h>
#include <mpd/client.h>

#define print_s(name, s) if (s != NULL) printf("%s: %s\n", name, s);

DBusConnection* dbus;

typedef enum {
    LOW = 0,
    NORMAL = 1,
    CRITICAL = 2
} NotifyUrgency;

void print_mpd_serveur_info(struct mpd_connection* conn)
{
    struct mpd_stats* stats;

    printf("Server version: %u\n", *mpd_connection_get_server_version(conn));

    if ((stats = mpd_run_stats(conn)) == NULL)
    {
        puts("Error: Connot get stats from mpd server");
        return;
    }

    printf("Number of artists %u\n", mpd_stats_get_number_of_artists(stats));

    mpd_stats_free(stats);
}

static void send_notification(const char* desc, const char* body, NotifyUrgency urgency)
{
    DBusMessage* msg;
    DBusMessageIter imsg;
    DBusMessageIter idicmsg;
    DBusMessageIter iarrdicmsg;
    DBusMessageIter iarrmsg;
    DBusMessageIter variant;
    dbus_uint32_t serial = 0;

    const char* m[4] =  {"1", "next", "2", "stop"};
    const char** s_m = m;
    const char* program = "mpdnotify";
    const char* icon = "";
    const char* urg = "urgency";
    const unsigned char urgl= (unsigned char)urgency;
    const dbus_uint32_t t1 = 0; // replace id, 0 is no replacement
    const dbus_int32_t t2 = -1; // expire timeout, -1 is default


    msg = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
    dbus_message_iter_init_append(msg, &imsg);

    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_STRING, &program);
    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_UINT32, &t1);
    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_STRING, &icon);
    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_STRING, &desc);
    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_STRING, &body);

    dbus_message_iter_open_container(&imsg, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &iarrmsg);
    dbus_message_iter_append_basic(&iarrmsg, DBUS_TYPE_STRING, &s_m[0]);
    dbus_message_iter_append_basic(&iarrmsg, DBUS_TYPE_STRING, &s_m[1]);
    dbus_message_iter_append_basic(&iarrmsg, DBUS_TYPE_STRING, &s_m[2]);
    dbus_message_iter_append_basic(&iarrmsg, DBUS_TYPE_STRING, &s_m[3]);
    dbus_message_iter_close_container(&imsg, &iarrmsg);

    dbus_message_iter_open_container(&imsg, DBUS_TYPE_ARRAY, DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &iarrdicmsg);
    dbus_message_iter_open_container(&iarrdicmsg, DBUS_TYPE_DICT_ENTRY, NULL, &idicmsg);
    dbus_message_iter_append_basic(&idicmsg, DBUS_TYPE_STRING, &urg);
    dbus_message_iter_open_container(&idicmsg, DBUS_TYPE_VARIANT, DBUS_TYPE_BYTE_AS_STRING, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BYTE, &urgl);
    dbus_message_iter_close_container(&idicmsg, &variant);
    dbus_message_iter_close_container(&iarrdicmsg, &idicmsg);
    dbus_message_iter_close_container(&imsg, &iarrdicmsg);

    dbus_message_iter_append_basic(&imsg, DBUS_TYPE_INT32, &t2);

    if (! dbus_connection_send(dbus, msg, &serial))
    {
        fprintf(stderr, "Out Of Memory!\n");
    }
    dbus_connection_flush(dbus);

    printf("Signal Sent\n");

    dbus_message_unref(msg);
}


DBusConnection* dbus_init_session()
{
    DBusConnection *cbus = NULL;
    DBusError error;

    dbus_error_init(&error);
    cbus = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "%s", error.message);
        dbus_error_free(&error);
    }

    dbus_bus_request_name(cbus, "fr.polms.mpdnotify", DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Error while claiming name (%s)\n", error.message);
        dbus_error_free(&error);
    }
    print_s("My dbus name is", dbus_bus_get_unique_name(cbus));

    return cbus;
}

void* task_listen_event(void* arg)
{
    struct mpd_connection* conn = (struct mpd_connection*)arg;
    struct mpd_song* song = NULL;
    const char* song_name = NULL;
    const char* artist_name = NULL;
    const char* album_name = NULL;
    char* notify_body;
    int notify_body_size = 0;
    enum mpd_idle idle;

    while (1)
    {
        idle = mpd_run_idle(conn);
        if (mpd_idle_name(idle) != NULL) {
            puts(mpd_idle_name(idle));
        } else {
            printf("%d\n", idle);
        }
        song = mpd_run_current_song(conn);
        if (song == NULL)
            continue;

        puts(mpd_song_get_uri(song));
        song_name = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
        artist_name = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
        album_name = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
        print_s("Title", song_name);
        print_s("Artist", artist_name);
        print_s("Album", album_name);
        if (idle == 12 || idle == MPD_IDLE_PLAYER) {
            notify_body_size = strlen(song_name) + strlen(artist_name) + 4;
            notify_body = malloc(notify_body_size);
            snprintf(notify_body, notify_body_size, "%s - %s", song_name, artist_name);
            send_notification("Song changed", notify_body, LOW);
            free(notify_body);
        }
        mpd_song_free(song);
    }
}

int main(int argc, char *argv[])
{
    pthread_t idle_listener;
    (void) argc;
    (void) argv;

    struct mpd_connection* conn;

    conn = mpd_connection_new("localhost", 6600, 15000);

    switch(mpd_connection_get_error(conn))
    {
    case MPD_ERROR_SUCCESS:
        break;
    default:
        printf("Error: %s\n", mpd_connection_get_error_message(conn));
    }

    print_mpd_serveur_info(conn);

    dbus = dbus_init_session();

    pthread_create(&idle_listener, NULL, task_listen_event, (void*) conn);

    sleep(5000);

    mpd_connection_free(conn);

    return EXIT_SUCCESS;
}
