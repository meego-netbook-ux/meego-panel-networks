/*
 * OpenConnect (SSL + DTLS) VPN client
 *
 * Copyright © 2008-2010 Intel Corporation.
 *
 * Authors: Jussi Kukkonen <jku@linux.intel.com>
 *          David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to:
 *
 *   Free Software Foundation, Inc.
 *   51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <getopt.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <gconf/gconf-client.h>
#include <dbus/dbus-glib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "auth-dlg-settings.h"

#include "openconnect.h"

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/ui.h>

static GConfClient *_gcl;
static char *_config_path;

static char *last_message;

static char *lasthost;

typedef struct vpnhost {
	char *hostname;
	char *hostaddress;
	char *usergroup;
	struct vpnhost *next;
} vpnhost;

vpnhost *vpnhosts;

enum certificate_response{
	CERT_DENIED = -1,
	CERT_USER_NOT_READY,
	CERT_ACCEPTED,
};

struct gconf_key {
	char *key;
	char *value;
	struct gconf_key *next;
};

typedef struct auth_ui_data {
	char *vpn_name;
	struct openconnect_info *vpninfo;
	struct gconf_key *success_keys;
	GtkWidget *dialog;
	GtkWidget *combo;
	GtkWidget *connect_button;
	GtkWidget *no_form_label;
	GtkWidget *getting_form_label;
	GtkWidget *ssl_box;
	GtkWidget *cancel_button;
	GtkWidget *login_button;
	GtkTextBuffer *log;

	int retval;
	int cookie_retval;

	gboolean cancelled; /* fully cancel the whole challenge-response series */
	gboolean getting_cookie;

	int form_grabbed;
	GQueue *form_entries; /* modified from worker thread */
	GMutex *form_mutex;

	GCond *form_retval_changed;
	gpointer form_retval;

	GCond *form_shown_changed;
	gboolean form_shown;

	GCond *cert_response_changed;
	enum certificate_response cert_response;
} auth_ui_data;

enum {
	AUTH_DIALOG_RESPONSE_LOGIN = 1,
	AUTH_DIALOG_RESPONSE_CANCEL,
} auth_dialog_response;



/* this is here because ssl ui (*opener) does not have a userdata pointer... */
static auth_ui_data *_ui_data;

static void connect_host(auth_ui_data *ui_data);

static void container_child_remove(GtkWidget *widget, gpointer data)
{
	GtkContainer *container = GTK_CONTAINER(data);

	gtk_container_remove(container, widget);
}

static void ssl_box_add_error(auth_ui_data *ui_data, const char *msg)
{
	GtkWidget *hbox, *text, *image;
	int width;

	hbox = gtk_hbox_new(FALSE, 8);
	gtk_box_pack_start(GTK_BOX(ui_data->ssl_box), hbox, FALSE, FALSE, 0);

	image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_ERROR,
					 GTK_ICON_SIZE_DIALOG);
	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);

	text = gtk_label_new(msg);
	gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
	gtk_window_get_size(GTK_WINDOW(ui_data->dialog), &width, NULL);
	/* FIXME: this is not very nice -- can't make the window thinner after this */
	gtk_widget_set_size_request(text, width - 100, -1);
	gtk_box_pack_start(GTK_BOX(hbox), text, FALSE, FALSE, 0);
}

static void ssl_box_add_info(auth_ui_data *ui_data, const char *msg)
{
	GtkWidget *text;
	int width;

	text = gtk_label_new(msg);
	gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
	gtk_window_get_size(GTK_WINDOW(ui_data->dialog), &width, NULL);
	/* FIXME: this is not very nice -- can't make the window thinner after this */
	gtk_widget_set_size_request(text, width - 40, -1);
	gtk_box_pack_start(GTK_BOX(ui_data->ssl_box), text, FALSE, FALSE, 0);
}

static void ssl_box_clear(auth_ui_data *ui_data)
{
	gtk_widget_hide(ui_data->no_form_label);
	gtk_widget_hide(ui_data->getting_form_label);
	gtk_container_foreach(GTK_CONTAINER(ui_data->ssl_box),
			      container_child_remove, ui_data->ssl_box);
	gtk_widget_set_sensitive (ui_data->login_button, FALSE);
	gtk_widget_set_sensitive (ui_data->cancel_button, FALSE);
}

typedef struct ui_fragment_data {
	GtkWidget *widget;
	auth_ui_data *ui_data;
	UI_STRING *uis;
	struct oc_form_opt *opt;
	char *entry_text;
	int grab_focus;
} ui_fragment_data;

static void entry_activate_cb(GtkWidget *widget, auth_ui_data *ui_data)
{
	gtk_dialog_response(GTK_DIALOG(ui_data->dialog), AUTH_DIALOG_RESPONSE_LOGIN);
}

static void do_check_visibility(ui_fragment_data *data, gboolean *visible)
{
	int min_len;

	if (!data->uis)
		return;

	min_len = UI_get_result_minsize(data->uis);

	if (min_len && (!data->entry_text || strlen(data->entry_text) < min_len))
		*visible = FALSE;
}

static void evaluate_login_visibility(auth_ui_data *ui_data)
{
	gboolean visible = TRUE;
	g_queue_foreach(ui_data->form_entries, (GFunc)do_check_visibility,
			&visible);

	gtk_widget_set_sensitive (ui_data->login_button, visible);
}

static void entry_changed(GtkEntry *entry, ui_fragment_data *data)
{
	g_free (data->entry_text);
	data->entry_text = g_strdup(gtk_entry_get_text(entry));
	evaluate_login_visibility(data->ui_data);
}

static void do_override_label(ui_fragment_data *data, struct oc_choice *choice)
{
	const char *new_label = data->opt->label;

	if (!data->widget)
		return;

	if (choice->override_name && !strcmp(choice->override_name, data->opt->name))
		    new_label = choice->override_label;

	gtk_label_set_text(GTK_LABEL(data->widget), new_label);

}
static void combo_changed(GtkComboBox *combo, ui_fragment_data *data)
{
	struct oc_form_opt_select *sopt = (void *)data->opt;
	int entry = gtk_combo_box_get_active(combo);
	if (entry < 0)
		return;

	data->entry_text = sopt->choices[entry].name;

	g_queue_foreach(data->ui_data->form_entries, (GFunc)do_override_label,
			&sopt->choices[entry]);
}

static gboolean ui_write_error (ui_fragment_data *data)
{
	ssl_box_add_error(data->ui_data, UI_get0_output_string(data->uis));

	g_slice_free (ui_fragment_data, data);

	return FALSE;
}

static gboolean ui_write_info (ui_fragment_data *data)
{
	ssl_box_add_info(data->ui_data, UI_get0_output_string(data->uis));

	g_slice_free (ui_fragment_data, data);

	return FALSE;
}

static gboolean ui_write_prompt (ui_fragment_data *data)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	GtkWidget *hbox, *text, *entry;
	int visible;
	const char *label;

	if (data->uis) {
		label = UI_get0_output_string(data->uis);
		visible = UI_get_input_flags(data->uis) & UI_INPUT_FLAG_ECHO;
	} else {
		label = data->opt->label;
		visible = (data->opt->type == OC_FORM_OPT_TEXT);
	}

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(data->ui_data->ssl_box), hbox, FALSE, FALSE, 0);

	text = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(hbox), text, FALSE, FALSE, 0);
	data->widget = text;

	entry = gtk_entry_new();
	gtk_box_pack_end(GTK_BOX(hbox), entry, FALSE, FALSE, 0);
	if (!visible)
		gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	if (data->entry_text)
		gtk_entry_set_text(GTK_ENTRY(entry), data->entry_text);
	if (!data->entry_text && !data->ui_data->form_grabbed) {
		data->ui_data->form_grabbed = 1;
		gtk_widget_grab_focus (entry);
	}
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(entry_changed), data);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(entry_activate_cb), ui_data);

	/* data is freed in ui_flush in worker thread */

	return FALSE;
}

static gboolean ui_add_select (ui_fragment_data *data)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	GtkWidget *hbox, *text, *combo;
	struct oc_form_opt_select *sopt = (void *)data->opt;
	int i;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(data->ui_data->ssl_box), hbox, FALSE, FALSE, 0);

	text = gtk_label_new(data->opt->label);
	gtk_box_pack_start(GTK_BOX(hbox), text, FALSE, FALSE, 0);

	combo = gtk_combo_box_text_new ();
	gtk_box_pack_end(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
	for (i = 0; i < sopt->nr_choices; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), sopt->choices[i].label);
		if (data->entry_text && 
		    !strcmp(data->entry_text, sopt->choices[i].name)) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(combo), i);
			g_free(data->entry_text);
			data->entry_text = sopt->choices[i].name;
		}
	}
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) < 0) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0); 
		data->entry_text = sopt->choices[0].name;
	}

	if (g_queue_peek_tail(ui_data->form_entries) == data)
		gtk_widget_grab_focus (combo);
	g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(combo_changed), data);
	/* Hook up the 'show' signal to ensure that we override prompts on 
	   UI elements which may be coming later. */
	g_signal_connect(G_OBJECT(combo), "show", G_CALLBACK(combo_changed), data);

	/* data is freed in ui_flush in worker thread */

	return FALSE;
}

static gboolean ui_show (auth_ui_data *ui_data)
{
	gtk_widget_hide (ui_data->getting_form_label);
	gtk_widget_show_all (ui_data->ssl_box);
	gtk_widget_set_sensitive (ui_data->cancel_button, TRUE);
	g_mutex_lock (ui_data->form_mutex);
	evaluate_login_visibility(ui_data);
	ui_data->form_shown = TRUE;
	g_cond_signal (ui_data->form_shown_changed);
	g_mutex_unlock (ui_data->form_mutex);

	return FALSE;
}

/* runs in worker thread */
static int ui_open(UI *ui)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */

	UI_add_user_data(ui, ui_data);

	return 1;
}

/* runs in worker thread */
static int ui_write(UI *ui, UI_STRING *uis)
{
	auth_ui_data *ui_data;
	ui_fragment_data *data;

	ui_data = UI_get0_user_data(ui);

	/* return if a new host has been selected */
	if (ui_data->cancelled) {
		return 1;
	}

	data = g_slice_new0 (ui_fragment_data);
	data->ui_data = ui_data;
	data->uis = uis;

	switch(UI_get_string_type(uis)) {
	case UIT_ERROR:
		g_idle_add ((GSourceFunc)ui_write_error, data);
		break;

	case UIT_INFO:
		g_idle_add ((GSourceFunc)ui_write_info, data);
		break;

	case UIT_PROMPT:
	case UIT_VERIFY:
		g_mutex_lock (ui_data->form_mutex);
		g_queue_push_head(ui_data->form_entries, data);
		g_mutex_unlock (ui_data->form_mutex);

		g_idle_add ((GSourceFunc)ui_write_prompt, data);
		break;

	case UIT_BOOLEAN:
		/* FIXME */
	case UIT_NONE:
	default:
		g_slice_free (ui_fragment_data, data);
	}
	return 1;
}

/* runs in worker thread */
static int ui_flush(UI* ui)
{
	auth_ui_data *ui_data;
	int response;

	ui_data = UI_get0_user_data(ui);

	g_idle_add((GSourceFunc)ui_show, ui_data);
	g_mutex_lock(ui_data->form_mutex);
	/* wait for ui to show */
	while (!ui_data->form_shown) {
		g_cond_wait(ui_data->form_shown_changed, ui_data->form_mutex);
	}
	ui_data->form_shown = FALSE;

	if (!ui_data->cancelled) {
		/* wait for form submission or cancel */
		while (!ui_data->form_retval) {
			g_cond_wait(ui_data->form_retval_changed, ui_data->form_mutex);
		}
		response = GPOINTER_TO_INT (ui_data->form_retval);
		ui_data->form_retval = NULL;
	} else
		response = AUTH_DIALOG_RESPONSE_CANCEL;

	/* set entry results and free temporary data structures */
	while (!g_queue_is_empty (ui_data->form_entries)) {
		ui_fragment_data *data;
		data = g_queue_pop_tail (ui_data->form_entries);
		if (data->entry_text) {
			UI_set_result(ui, data->uis, data->entry_text);
		}
		g_slice_free (ui_fragment_data, data);
	}
	ui_data->form_grabbed = 0;
	g_mutex_unlock(ui_data->form_mutex);

	/* -1 = cancel,
	 *  0 = failure,
	 *  1 = success */
	return (response == AUTH_DIALOG_RESPONSE_LOGIN ? 1 : -1);
}

/* runs in worker thread */
static int ui_close(UI *ui)
{
	return 1;
}

static int init_openssl_ui(void)
{
	UI_METHOD *ui_method = UI_create_method("OpenConnect VPN UI (gtk)");

	UI_method_set_opener(ui_method, ui_open);
	UI_method_set_flusher(ui_method, ui_flush);
	UI_method_set_writer(ui_method, ui_write);
	UI_method_set_closer(ui_method, ui_close);

	UI_set_default_method(ui_method);
	return 0;
}

static void remember_gconf_key(auth_ui_data *ui_data, char *key, char *value)
{
	struct gconf_key *k = g_malloc(sizeof(*k));

	if (!k)
		return;

	k->next = ui_data->success_keys;
	k->key = key;
	k->value = value;

	ui_data->success_keys = k;
	while (k->next) {
		if (!strcmp(k->next->key, key)) {
			struct gconf_key *old = k->next;
			k->next = old->next;
			g_free(old->key);
			g_free(old->value);
			g_free(old);
			break;
		}
		k = k->next;
	}
}

static char *find_form_answer(struct oc_auth_form *form, struct oc_form_opt *opt)
{
	char *config_path = _config_path; /* FIXME global */
	GConfClient *gcl = _gcl; /* FIXME global */
	char *key, *result;
	key = g_strdup_printf("%s/vpn/form:%s:%s", config_path,
			      form->auth_id, opt->name);
	result = gconf_client_get_string(gcl, key, NULL);
	g_free(key);
	return result;
}

/* This part for processing forms from openconnect directly, rather than
   through the SSL UI abstraction (which doesn't allow 'select' options) */

static gboolean ui_form (struct oc_auth_form *form)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	struct oc_form_opt *opt;

	ssl_box_clear(ui_data);

	g_mutex_lock(ui_data->form_mutex);
	while (!g_queue_is_empty (ui_data->form_entries)) {
		ui_fragment_data *data;
		data = g_queue_pop_tail (ui_data->form_entries);
		g_slice_free (ui_fragment_data, data);
	}
	g_mutex_unlock(ui_data->form_mutex);

	if (form->banner)
		ssl_box_add_info(ui_data, form->banner);
	if (form->error)
		ssl_box_add_error(ui_data, form->error);
	if (form->message)
		ssl_box_add_info(ui_data, form->message);

	for (opt = form->opts; opt; opt = opt->next) {
		ui_fragment_data *data;

		if (opt->type == OC_FORM_OPT_HIDDEN)
			continue;

		data = g_slice_new0 (ui_fragment_data);
		data->ui_data = ui_data;
		data->opt = opt;
		
		if (opt->type == OC_FORM_OPT_PASSWORD ||
		    opt->type == OC_FORM_OPT_TEXT) {
			g_mutex_lock (ui_data->form_mutex);
			g_queue_push_head(ui_data->form_entries, data);
			g_mutex_unlock (ui_data->form_mutex);
			if (opt->type != OC_FORM_OPT_PASSWORD)
				data->entry_text = find_form_answer(form, opt);

			ui_write_prompt(data);
		} else if (opt->type == OC_FORM_OPT_SELECT) {
			g_mutex_lock (ui_data->form_mutex);
			g_queue_push_head(ui_data->form_entries, data);
			g_mutex_unlock (ui_data->form_mutex);
			data->entry_text = find_form_answer(form, opt);

			ui_add_select(data);
		} else
			g_slice_free (ui_fragment_data, data);
	}
	
	return ui_show(ui_data);
}

static int nm_process_auth_form (struct openconnect_info *vpninfo,
				 struct oc_auth_form *form)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	int response;

	g_idle_add((GSourceFunc)ui_form, form);

	g_mutex_lock(ui_data->form_mutex);
	/* wait for ui to show */
	while (!ui_data->form_shown) {
		g_cond_wait(ui_data->form_shown_changed, ui_data->form_mutex);
	}
	ui_data->form_shown = FALSE;

	if (!ui_data->cancelled) {
		/* wait for form submission or cancel */
		while (!ui_data->form_retval) {
			g_cond_wait(ui_data->form_retval_changed, ui_data->form_mutex);
		}
		response = GPOINTER_TO_INT (ui_data->form_retval);
		ui_data->form_retval = NULL;
	} else
		response = AUTH_DIALOG_RESPONSE_CANCEL;

	if (response == AUTH_DIALOG_RESPONSE_LOGIN) {
		/* set entry results and free temporary data structures */
		while (!g_queue_is_empty (ui_data->form_entries)) {
			ui_fragment_data *data;
			data = g_queue_pop_tail (ui_data->form_entries);
			if (data->entry_text) {
				data->opt->value = data->entry_text;

				if (data->opt->type == OC_FORM_OPT_TEXT ||
				    data->opt->type == OC_FORM_OPT_SELECT) {
					char *keyname;
					keyname = g_strdup_printf("form:%s:%s", form->auth_id, data->opt->name);
					remember_gconf_key(ui_data, keyname, strdup(data->entry_text));
				}
			}
			g_slice_free (ui_fragment_data, data);
		}
	}


	g_mutex_unlock(ui_data->form_mutex);
	
	/* -1 = cancel,
	 *  0 = failure,
	 *  1 = success */
	return (response == AUTH_DIALOG_RESPONSE_LOGIN ? 0 : 1);

}

static char* get_title(const char *vpn_name)
{
	if (vpn_name)
		return g_strdup_printf("Connect to VPN '%s'", vpn_name);
	else
		return g_strdup("Connect to VPN");
}

typedef struct cert_data {
	auth_ui_data *ui_data;
	X509 *peer_cert;
	const char *reason;
} cert_data;


static gboolean user_validate_cert(cert_data *data)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	BIO *bp = BIO_new(BIO_s_mem());
	char *msg, *title;
	BUF_MEM *certinfo;
	char zero = 0;
	GtkWidget *dlg, *text, *scroll;
	GtkTextBuffer *buffer;
	int result;

	/* There are probably better ways to do this -- getting individual
	   elements of the cert info and formatting it nicely in the dialog
	   box. But this will do for now... */
	X509_print_ex(bp, data->peer_cert, 0, 0);
	BIO_write(bp, &zero, 1);
	BIO_get_mem_ptr(bp, &certinfo);

	title = get_title(data->ui_data->vpn_name);
	msg = g_strdup_printf(_("Certificate from VPN server \"%s\" failed verification.\n"
			      "Reason: %s\nDo you want to accept it?"),
			      openconnect_get_hostname(data->ui_data->vpninfo),
			      data->reason);

	dlg = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_QUESTION,
				     GTK_BUTTONS_OK_CANCEL,
				     msg);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dlg), FALSE);
	gtk_window_set_skip_pager_hint(GTK_WINDOW(dlg), FALSE);
	gtk_window_set_title(GTK_WINDOW(dlg), title);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 550, 600);
	gtk_window_set_resizable(GTK_WINDOW(dlg), TRUE);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);

	g_free(title);
	g_free(msg);

	scroll = gtk_scrolled_window_new(NULL, NULL);

	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG(dlg))), scroll, TRUE, TRUE, 0);

	gtk_widget_show(scroll);

	text = gtk_text_view_new();
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_set_text(buffer, certinfo->data, -1);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), 0);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
	gtk_container_add(GTK_CONTAINER(scroll), text);
	gtk_widget_show(text);

	result = gtk_dialog_run(GTK_DIALOG(dlg));

	BIO_free(bp);
	gtk_widget_destroy(dlg);

	g_mutex_lock (ui_data->form_mutex);
	if (result == GTK_RESPONSE_OK)
		data->ui_data->cert_response = CERT_ACCEPTED;
	else
		data->ui_data->cert_response = CERT_DENIED;
	g_cond_signal (ui_data->cert_response_changed);
	g_mutex_unlock (ui_data->form_mutex);

	return FALSE;
}

/* runs in worker thread */
static int validate_peer_cert(struct openconnect_info *vpninfo,
			      X509 *peer_cert, const char *reason)
{
	char *config_path = _config_path; /* FIXME global */
	GConfClient *gcl = _gcl; /* FIXME global */
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	char fingerprint[EVP_MAX_MD_SIZE * 2 + 1];
	char *certs_data;
	char *key;
	int ret = 0;
	cert_data *data;

	ret = openconnect_get_cert_sha1(vpninfo, peer_cert, fingerprint);
	if (ret)
		return ret;

	key = g_strdup_printf("%s/vpn/%s", config_path, "certsigs");
	certs_data = gconf_client_get_string(gcl, key, NULL);
	if (certs_data) {
		char **certs = g_strsplit_set(certs_data, "\t", 0);
		char **this = certs;

		while (*this) {
			if (!strcmp(*this, fingerprint)) {
				g_strfreev(certs);
				goto out;
			}
			this++;
		}
		g_strfreev(certs);
	}

	data = g_slice_new(cert_data);
	data->ui_data = ui_data; /* FIXME uses global */
	data->peer_cert = peer_cert;
	data->reason = reason;

	g_mutex_lock(ui_data->form_mutex);

	ui_data->cert_response = CERT_USER_NOT_READY;
	g_idle_add((GSourceFunc)user_validate_cert, data);

	/* wait for user to accept or cancel */
	while (ui_data->cert_response == CERT_USER_NOT_READY) {
		g_cond_wait(ui_data->cert_response_changed, ui_data->form_mutex);
	}
	if (ui_data->cert_response == CERT_ACCEPTED) {
		if (certs_data) {
			char *new = g_strdup_printf("%s\t%s", certs_data, fingerprint);
			gconf_client_set_string(gcl, key, new, NULL);
			g_free(new);
		} else {
			gconf_client_set_string(gcl, key, fingerprint, NULL);
		}
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	g_mutex_unlock (ui_data->form_mutex);

	g_slice_free(cert_data, data);

 out:
	g_free(certs_data);
	g_free(key);
	return ret;
}

static char *get_config_path(GConfClient *gcl, const char *vpn_uuid)
{
	GSList *connections, *this;
	char *key, *val;
	char *config_path = NULL;

	connections = gconf_client_all_dirs(gcl,
					    "/system/networking/connections",
					    NULL);

	for (this = connections; this; this = this->next) {
		const char *path = (const char *) this->data;

		key = g_strdup_printf("%s/connection/type", path);
		val = gconf_client_get_string(gcl, key, NULL);
		g_free(key);

		if (!val || strcmp(val, "vpn")) {
			g_free(val);
			continue;
		}
		g_free(val);

		key = g_strdup_printf("%s/connection/uuid", path);
		val = gconf_client_get_string(gcl, key, NULL);
		g_free(key);

		if (!val || strcmp(val, vpn_uuid)) {
			g_free(val);
			continue;
		}
		g_free(val);

		config_path = g_strdup(path);
		break;
	}
	g_slist_foreach(connections, (GFunc)g_free, NULL);
	g_slist_free(connections);

	return config_path;
}

static char *get_gconf_setting(GConfClient *gcl, char *config_path,
			       char *setting)
{
	char *result;
	char *key = g_strdup_printf("%s/vpn/%s", config_path, setting);
	result = gconf_client_get_string(gcl, key, NULL);
	g_free(key);
	return result;
}

static int get_gconf_autoconnect(GConfClient *gcl, char *config_path)
{
	char *autoconnect = get_gconf_setting(gcl, config_path, "autoconnect");
	int ret = 0;

	if (autoconnect) {
		if (!strcmp(autoconnect, "yes"))
			ret = 1;
		g_free(autoconnect);
	}
	return ret;
}

static int parse_xmlconfig(char *xmlconfig)
{
	xmlDocPtr xml_doc;
	xmlNode *xml_node, *xml_node2;
	struct vpnhost *newhost, **list_end;

	list_end = &vpnhosts->next;
	/* gateway may be there already */
	while (*list_end) {
		list_end = &(*list_end)->next;
	}

	xml_doc = xmlReadMemory(xmlconfig, strlen(xmlconfig), "noname.xml", NULL, 0);

	xml_node = xmlDocGetRootElement(xml_doc);
	for (xml_node = xml_node->children; xml_node; xml_node = xml_node->next) {
                if (xml_node->type == XML_ELEMENT_NODE &&
                    !strcmp((char *)xml_node->name, "ServerList")) {

                        for (xml_node = xml_node->children; xml_node;
                             xml_node = xml_node->next) {

                                if (xml_node->type == XML_ELEMENT_NODE &&
                                    !strcmp((char *)xml_node->name, "HostEntry")) {
                                        int match = 0;

					newhost = malloc(sizeof(*newhost));
					if (!newhost)
						return -ENOMEM;

					memset(newhost, 0, sizeof(*newhost));
                                        for (xml_node2 = xml_node->children;
                                             match >= 0 && xml_node2; xml_node2 = xml_node2->next) {

                                                if (xml_node2->type != XML_ELEMENT_NODE)
                                                        continue;

                                                if (!strcmp((char *)xml_node2->name, "HostName")) {
                                                        char *content = (char *)xmlNodeGetContent(xml_node2);
							newhost->hostname = content;
						} else if (!strcmp((char *)xml_node2->name, "HostAddress")) {
                                                        char *content = (char *)xmlNodeGetContent(xml_node2);
							newhost->hostaddress = content;
						} else if (!strcmp((char *)xml_node2->name, "UserGroup")) {
                                                        char *content = (char *)xmlNodeGetContent(xml_node2);
							newhost->usergroup = content;
						}
					}
					if (newhost->hostname && newhost->hostaddress) {
						*list_end = newhost;
						list_end = &newhost->next;

						if (!strcasecmp(newhost->hostaddress, vpnhosts->hostaddress) &&
						    !strcasecmp(newhost->usergroup ?: "", vpnhosts->usergroup ?: "")) {
							/* Remove originally configured host if it's in the list */
							struct vpnhost *tmp = vpnhosts->next;
							free(vpnhosts);
							vpnhosts = tmp;
						}

                                        } else
						free(newhost);
                                }
                        }
			break;
                }
        }
        xmlFreeDoc(xml_doc);
	return 0;
}

static int get_config(char *vpn_uuid, struct openconnect_info *vpninfo)
{
	GConfClient *gcl;
	char *config_path;
	char *proxy;
	char *xmlconfig;
	char *hostname;
	char *group;
	char *csd;
	char *sslkey, *cert;
	char *csd_wrapper;
	char *pem_passphrase_fsid;

	_gcl = gcl = gconf_client_get_default();
	_config_path = config_path = get_config_path(gcl, vpn_uuid);

	if (!config_path)
		return -EINVAL;

	hostname = get_gconf_setting(gcl, config_path,
				     NM_OPENCONNECT_KEY_GATEWAY);
	if (!hostname) {
		fprintf(stderr, "No gateway configured\n");
		return -EINVAL;
	}

	/* add gateway to host list */
	vpnhosts = malloc(sizeof(*vpnhosts));
	if (!vpnhosts)
		return -ENOMEM;
	vpnhosts->hostname = g_strdup(hostname);
	group = strchr(hostname, '/');
	if (group) {
		*(group++) = 0;
		vpnhosts->usergroup = g_strdup(group);
	} else
		vpnhosts->usergroup = NULL;
	vpnhosts->hostaddress = hostname;
	vpnhosts->next = NULL;

if (0) {
/* DEBUG add another copy of gateway to host list */
	vpnhost *tmphost;
	tmphost = malloc(sizeof(tmphost));
	if (!tmphost)
		return -ENOMEM;
	tmphost->hostname = g_strdup("VPN Gateway 2");
	tmphost->hostaddress = hostname;
	tmphost->usergroup = NULL;
	tmphost->next = NULL;
	vpnhosts->next = tmphost;
}
	lasthost = get_gconf_setting(gcl, config_path, "lasthost");

	xmlconfig = get_gconf_setting(gcl, config_path, NM_OPENCONNECT_KEY_XMLCONFIG);
	if (xmlconfig) {
		unsigned char sha1[SHA_DIGEST_LENGTH];
		char sha1_text[SHA_DIGEST_LENGTH * 2];
		EVP_MD_CTX c;
		int i;

		EVP_MD_CTX_init(&c);
		EVP_Digest(xmlconfig, strlen(xmlconfig), sha1, NULL, EVP_sha1(), NULL);
		EVP_MD_CTX_cleanup(&c);

		for (i = 0; i < SHA_DIGEST_LENGTH; i++)
			sprintf(&sha1_text[i*2], "%02x", sha1[i]);

		openconnect_set_xmlsha1(vpninfo, sha1_text, sizeof(sha1_text));
		parse_xmlconfig(xmlconfig);
		g_free(xmlconfig);
	}

	openconnect_set_cafile(vpninfo,
			       get_gconf_setting(gcl, config_path, NM_OPENCONNECT_KEY_CACERT));

	csd = get_gconf_setting(gcl, config_path, "enable_csd_trojan");
	if (csd && !strcmp(csd, "yes")) {
		/* We're not running as root; we can't setuid(). */
		csd_wrapper = get_gconf_setting(gcl, config_path, "csd_wrapper");
		if (csd_wrapper && !csd_wrapper[0]) {
			g_free(csd_wrapper);
			csd_wrapper = NULL;
		}
		openconnect_setup_csd(vpninfo, getuid(), 1, csd_wrapper);
	}
	g_free(csd);

	proxy = get_gconf_setting(gcl, config_path, "proxy");
	if (proxy && proxy[0] && openconnect_set_http_proxy(vpninfo, proxy))
		return -EINVAL;

	cert = get_gconf_setting(gcl, config_path, NM_OPENCONNECT_KEY_USERCERT);
	sslkey = get_gconf_setting(gcl, config_path, NM_OPENCONNECT_KEY_PRIVKEY);
	openconnect_set_client_cert (vpninfo, cert, sslkey);

	pem_passphrase_fsid = get_gconf_setting(gcl, config_path, "pem_passphrase_fsid");
	if (pem_passphrase_fsid && cert && !strcmp(pem_passphrase_fsid, "yes"))
		openconnect_passphrase_from_fsid(vpninfo);
	g_free(pem_passphrase_fsid);

	return 0;
}

static void populate_vpnhost_combo(auth_ui_data *ui_data)
{
	struct vpnhost *host;
	int i = 0;
	GtkComboBox *combo = GTK_COMBO_BOX(ui_data->combo);

	for (host = vpnhosts; host; host = host->next) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo),
                                                host->hostname);

		if (i == 0 ||
		    (lasthost && !strcmp(host->hostname, lasthost)))
			gtk_combo_box_set_active(combo, i);
		i++;

	}
}

static int write_new_config(struct openconnect_info *vpninfo, char *buf, int buflen)
{
	char *config_path = _config_path; /* FIXME global */
	GConfClient *gcl = _gcl; /* FIXME global */
	char *key = g_strdup_printf("%s/vpn/%s", config_path,
				    NM_OPENCONNECT_KEY_XMLCONFIG);
	gconf_client_set_string(gcl, key, buf, NULL);
	return 0;
}

static void autocon_toggled(GtkWidget *widget)
{
	char *config_path = _config_path; /* FIXME global */
	GConfClient *gcl = _gcl; /* FIXME global */
	int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	char *key = g_strdup_printf("%s/vpn/autoconnect", config_path);

	gconf_client_set_string(gcl, key, enabled ? "yes" : "no", NULL);
}

static void scroll_log(GtkTextBuffer *log, GtkTextView *view)
{
	GtkTextMark *mark;

	g_return_if_fail(GTK_IS_TEXT_VIEW(view));

	mark = gtk_text_buffer_get_insert(log);
	gtk_text_view_scroll_to_mark(view, mark, 0.0, FALSE, 0.0, 0.0);
}

/* NOTE: write_progress_real() will free the given string */
static gboolean write_progress_real(char *message)
{
	auth_ui_data *ui_data = _ui_data; /* FIXME global */
	GtkTextIter iter;

	g_return_val_if_fail(message, FALSE);

	gtk_text_buffer_get_end_iter(ui_data->log, &iter);
	gtk_text_buffer_insert(ui_data->log, &iter, message, -1);

	g_free(message);

	return FALSE;
}

/* runs in worker thread */
static void write_progress(struct openconnect_info *info, int level, const char *fmt, ...)
{
	va_list args;
	char *msg;

	if (last_message) {
		g_free(last_message);
		last_message = NULL;
	}

	va_start(args, fmt);
	msg = g_strdup_vprintf(fmt, args);
	va_end(args);

	if (level <= PRG_DEBUG) {
		g_idle_add((GSourceFunc)write_progress_real, g_strdup(msg));
	}

	if (level <= PRG_ERR) {
		last_message = msg;
		return;
	}
	g_free(msg);
}

static void print_peer_cert(struct openconnect_info *vpninfo)
{
	char fingerprint[EVP_MAX_MD_SIZE * 2 + 1];
	X509 *cert = openconnect_get_peer_cert(vpninfo);

	if (cert && !openconnect_get_cert_sha1(vpninfo, cert, fingerprint))
		printf("gwcert\n%s\n", fingerprint);
}

static gboolean cookie_obtained(auth_ui_data *ui_data)
{
	ui_data->getting_cookie = FALSE;
	gtk_widget_hide (ui_data->getting_form_label);

	if (ui_data->cancelled) {
		/* user has chosen a new host, start from beginning */
		while (ui_data->success_keys) {
			struct gconf_key *k = ui_data->success_keys;
			
			ui_data->success_keys = k->next;
			g_free(k->key);
			g_free(k->value);
			g_free(k);
		}			
		connect_host(ui_data);
		return FALSE;
	}

	if (ui_data->cookie_retval < 0) {
		/* error while getting cookie */
		if (last_message) {
			ssl_box_add_error(ui_data, last_message);
			gtk_widget_show_all(ui_data->ssl_box);
			gtk_widget_set_sensitive(ui_data->cancel_button, TRUE);
		}
		ui_data->retval = 1;
	} else if (!ui_data->cookie_retval) {
		/* got cookie */
		while (ui_data->success_keys) {
			char *config_path = _config_path; /* FIXME global */
			GConfClient *gcl = _gcl; /* FIXME global */
			struct gconf_key *k = ui_data->success_keys;
			char *key = g_strdup_printf("%s/vpn/%s", config_path, k->key);

			gconf_client_set_string(gcl, key, k->value, NULL);
			g_free(key);

			ui_data->success_keys = k->next;
			g_free(k->key);
			g_free(k->value);
			g_free(k);
		}

		printf("%s\n%s:%d\n", NM_OPENCONNECT_KEY_GATEWAY,
		       openconnect_get_hostname(ui_data->vpninfo),
		       openconnect_get_port(ui_data->vpninfo));
		printf("%s\n%s\n", NM_OPENCONNECT_KEY_COOKIE,
		       openconnect_get_cookie(ui_data->vpninfo));
		print_peer_cert(ui_data->vpninfo);
		openconnect_clear_cookie(ui_data->vpninfo);
		printf("\n\n");
		fflush(stdout);
		ui_data->retval = 0;

		gtk_main_quit();
	} else {
		/* no cookie; user cancellation */
		gtk_widget_show (ui_data->no_form_label);
		ui_data->retval = 1;
	}

	while (ui_data->success_keys) {
		struct gconf_key *k = ui_data->success_keys;

		ui_data->success_keys = k->next;
		g_free(k->key);
		g_free(k->value);
		g_free(k);
	}			

	return FALSE;
}

static gpointer obtain_cookie (auth_ui_data *ui_data)
{
	int ret;

	ret = openconnect_obtain_cookie(ui_data->vpninfo);

	ui_data->cookie_retval = ret;
	g_idle_add ((GSourceFunc)cookie_obtained, ui_data);

	return NULL;
}

static void connect_host(auth_ui_data *ui_data)
{
	GThread *thread;
	vpnhost *host;
	int i;
	int host_nr;

	ui_data->cancelled = FALSE;
	ui_data->getting_cookie = TRUE;

	g_mutex_lock (ui_data->form_mutex);
	ui_data->form_retval = NULL;
	g_mutex_unlock (ui_data->form_mutex);

	ssl_box_clear(ui_data);
	gtk_widget_show(ui_data->getting_form_label);

	/* reset ssl context.
	 * TODO: this is probably not the way to go... */
	openconnect_reset_ssl(ui_data->vpninfo);

	host_nr = gtk_combo_box_get_active(GTK_COMBO_BOX(ui_data->combo));
	host = vpnhosts;
	for (i = 0; i < host_nr; i++)
		host = host->next;

	if (openconnect_parse_url(ui_data->vpninfo, host->hostaddress)) {
		fprintf(stderr, "Failed to parse server URL '%s'\n",
			host->hostaddress);
		openconnect_set_hostname (ui_data->vpninfo, g_strdup(host->hostaddress));
	}

	if (!openconnect_get_urlpath(ui_data->vpninfo) && host->usergroup)
		openconnect_set_urlpath(ui_data->vpninfo, g_strdup(host->usergroup));

	remember_gconf_key(ui_data, g_strdup("lasthost"), g_strdup(host->hostname));

	thread = g_thread_create((GThreadFunc)obtain_cookie, ui_data,
				 FALSE, NULL);
	(void)thread;
}


static void queue_connect_host(auth_ui_data *ui_data)
{
	ssl_box_clear(ui_data);
	gtk_widget_show(ui_data->getting_form_label);
	gtk_widget_hide(ui_data->no_form_label);

	if (!ui_data->getting_cookie) {
		connect_host(ui_data);
	} else if (!ui_data->cancelled) {
		/* set state to cancelled. Current challenge-response-
		 * conversation will not be shown to user, and cookie_obtained()
		 * will start a new one conversation */
		ui_data->cancelled = TRUE;
		gtk_dialog_response(GTK_DIALOG(ui_data->dialog), AUTH_DIALOG_RESPONSE_CANCEL);
	}
}

static void dialog_response (GtkDialog *dialog, int response, auth_ui_data *ui_data)
{
	switch (response) {
	case AUTH_DIALOG_RESPONSE_CANCEL:
	case AUTH_DIALOG_RESPONSE_LOGIN:
		ssl_box_clear(ui_data);
		if (ui_data->getting_cookie)
			gtk_widget_show (ui_data->getting_form_label);
		g_mutex_lock (ui_data->form_mutex);
		ui_data->form_retval = GINT_TO_POINTER(response);
		g_cond_signal (ui_data->form_retval_changed);
		g_mutex_unlock (ui_data->form_mutex);
		break;
	case GTK_RESPONSE_CLOSE:
		gtk_main_quit();
		break;
	default:
		;
	}
}

static void cancel_clicked (GtkButton *btn, auth_ui_data *ui_data)
{
	gtk_dialog_response (GTK_DIALOG(ui_data->dialog), AUTH_DIALOG_RESPONSE_CANCEL);
}

static void login_clicked (GtkButton *btn, auth_ui_data *ui_data)
{
	gtk_dialog_response (GTK_DIALOG(ui_data->dialog), AUTH_DIALOG_RESPONSE_LOGIN);
}

static void build_main_dialog(auth_ui_data *ui_data)
{
	char *config_path = _config_path; /* FIXME global */
	GConfClient *gcl = _gcl; /* FIXME global */
	char *title;
	GtkWidget *vbox, *hbox, *label, *frame, *image, *frame_box;
	GtkWidget *exp, *scrolled, *view, *autocon;

	gtk_window_set_default_icon_name(GTK_STOCK_DIALOG_AUTHENTICATION);

	title = get_title(ui_data->vpn_name);
	ui_data->dialog = gtk_dialog_new_with_buttons(title, NULL, GTK_DIALOG_MODAL,
						      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
						      NULL);
	g_signal_connect (ui_data->dialog, "response", G_CALLBACK(dialog_response), ui_data);
	gtk_window_set_default_size(GTK_WINDOW(ui_data->dialog), 350, 300);
	g_signal_connect_swapped(ui_data->dialog, "destroy",
				 G_CALLBACK(gtk_main_quit), NULL);
	g_free(title);

	vbox = gtk_vbox_new(FALSE, 8);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area (GTK_DIALOG(ui_data->dialog))), vbox, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
	gtk_widget_show(vbox);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	label = gtk_label_new(_("VPN host"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	ui_data->combo = gtk_combo_box_text_new ();
	populate_vpnhost_combo(ui_data);
	gtk_box_pack_start(GTK_BOX(hbox), ui_data->combo, TRUE, TRUE, 0);
	g_signal_connect_swapped(ui_data->combo, "changed",
	                         G_CALLBACK(queue_connect_host), ui_data);
	gtk_widget_show(ui_data->combo);

	ui_data->connect_button = gtk_button_new();
	gtk_box_pack_end(GTK_BOX(hbox), ui_data->connect_button, FALSE, FALSE, 0);
	image = gtk_image_new_from_stock(GTK_STOCK_CONNECT, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON(ui_data->connect_button), image);
	gtk_widget_grab_focus(ui_data->connect_button);
	g_signal_connect_swapped(ui_data->connect_button, "clicked",
				 G_CALLBACK(queue_connect_host), ui_data);
	gtk_widget_show(ui_data->connect_button);

	autocon = gtk_check_button_new_with_label(_("Automatically start connecting next time"));
	gtk_box_pack_start(GTK_BOX(vbox), autocon, FALSE, FALSE, 0);
	if (get_gconf_autoconnect(gcl, config_path))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autocon), 1);
	g_signal_connect(autocon, "toggled", G_CALLBACK(autocon_toggled), NULL);
	gtk_widget_show(autocon);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	gtk_widget_set_size_request(frame, -1, -1);
	gtk_widget_show(frame);

	frame_box = gtk_vbox_new(FALSE, 4);
	gtk_container_set_border_width(GTK_CONTAINER(frame_box), 8);
	gtk_container_add(GTK_CONTAINER(frame), frame_box);
	gtk_widget_show(frame_box);

	ui_data->no_form_label = gtk_label_new(_("Select a host to fetch the login form"));
	gtk_widget_set_sensitive(ui_data->no_form_label, FALSE);
	gtk_box_pack_start(GTK_BOX(frame_box), ui_data->no_form_label, FALSE, FALSE, 0);
	gtk_widget_show(ui_data->no_form_label);

	ui_data->getting_form_label = gtk_label_new(_("Contacting host, please wait..."));
	gtk_widget_set_sensitive(ui_data->getting_form_label, FALSE);
	gtk_box_pack_start(GTK_BOX(frame_box), ui_data->getting_form_label, FALSE, FALSE, 0);

	ui_data->ssl_box = gtk_vbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(frame_box), ui_data->ssl_box, FALSE, FALSE, 0);
	gtk_widget_show(ui_data->ssl_box);

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_box_pack_end(GTK_BOX(frame_box), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	ui_data->login_button = gtk_button_new_with_mnemonic(_("_Login"));
	image = gtk_image_new_from_stock(GTK_STOCK_APPLY, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON(ui_data->login_button), image);
	gtk_box_pack_end(GTK_BOX(hbox), ui_data->login_button, FALSE, FALSE, 0);
	g_signal_connect (ui_data->login_button, "clicked", G_CALLBACK(login_clicked), ui_data);
	gtk_widget_set_sensitive (ui_data->login_button, FALSE);
	gtk_widget_show(ui_data->login_button);

	ui_data->cancel_button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
	gtk_box_pack_end(GTK_BOX(hbox), ui_data->cancel_button, FALSE, FALSE, 0);
	g_signal_connect (ui_data->cancel_button, "clicked", G_CALLBACK(cancel_clicked), ui_data);
	gtk_widget_set_sensitive (ui_data->cancel_button, FALSE);
	gtk_widget_show(ui_data->cancel_button);

	exp = gtk_expander_new(_("Log"));
	gtk_box_pack_end(GTK_BOX(vbox), exp, FALSE, FALSE, 0);
	gtk_widget_show(exp);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scrolled, -1, 75);
	gtk_container_add(GTK_CONTAINER(exp), scrolled);
	gtk_widget_show(scrolled);

	view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 5);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 5);
	gtk_text_view_set_indent(GTK_TEXT_VIEW(view), -10);
	gtk_container_add(GTK_CONTAINER(scrolled), view);
	gtk_widget_show(view);

	ui_data->log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	g_signal_connect(ui_data->log, "changed", G_CALLBACK(scroll_log), view);
}

static auth_ui_data *init_ui_data (char *vpn_name)
{
	auth_ui_data *ui_data;

	ui_data = g_slice_new0(auth_ui_data);
	ui_data->retval = 1;

	ui_data->form_entries = g_queue_new();
	ui_data->form_mutex = g_mutex_new();
	ui_data->form_retval_changed = g_cond_new();
	ui_data->form_shown_changed = g_cond_new();
	ui_data->cert_response_changed = g_cond_new();
	ui_data->vpn_name = vpn_name;

	ui_data->vpninfo = (void *)openconnect_vpninfo_new("OpenConnect VPN Agent (NetworkManager)",
						   validate_peer_cert, write_new_config,
						   nm_process_auth_form, write_progress);

#if 0
	ui_data->vpninfo->proxy_factory = px_proxy_factory_new();
#endif

	return ui_data;
}

static struct option long_options[] = {
	{"reprompt", 0, 0, 'r'},
	{"uuid", 1, 0, 'u'},
	{"name", 1, 0, 'n'},
	{"service", 1, 0, 's'},
	{NULL, 0, 0, 0},
};

int main (int argc, char **argv)
{
	char *vpn_name = NULL, *vpn_uuid = NULL, *vpn_service = NULL;
	int opt;

	while ((opt = getopt_long(argc, argv, "ru:n:s:", long_options, NULL))) {
		if (opt < 0)
			break;

		switch(opt) {
		case 'r':
			/* Reprompt does nothing */
			break;

		case 'u':
			vpn_uuid = optarg;
			break;

		case 'n':
			vpn_name = optarg;
			break;

		case 's':
			vpn_service = optarg;
			break;

		default:
			fprintf(stderr, "Unknown option\n");
			return 1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Superfluous command line options\n");
		return 1;
	}

	if (!vpn_uuid || !vpn_name || !vpn_service) {
		fprintf (stderr, "Have to supply UUID, name, and service\n");
		return 1;
	}

	if (strcmp(vpn_service, NM_DBUS_SERVICE_OPENCONNECT) != 0) {
		fprintf (stderr, "This dialog only works with the '%s' service\n",
			 NM_DBUS_SERVICE_OPENCONNECT);
		return 1;
	}

	g_thread_init (NULL);
	/* This shouldn't be necessary; we're working around a gconf bug.
	   http://bugzilla.meego.com/show_bug.cgi?id=918 */
	dbus_g_thread_init();
	gtk_init(0, NULL);

	_ui_data = init_ui_data(vpn_name);
	if (get_config(vpn_uuid, _ui_data->vpninfo)) {
		fprintf(stderr, "Failed to find VPN UUID %s in gconf\n", vpn_uuid);
		return 1;
	}
	build_main_dialog(_ui_data);

	init_openssl_ui();
	openconnect_init_openssl();

	if (get_gconf_autoconnect(_gcl, _config_path))
		queue_connect_host(_ui_data);

	gtk_window_present(GTK_WINDOW(_ui_data->dialog));
	gtk_main();

	return _ui_data->retval;
}
