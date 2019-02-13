#include "part_browser_window.hpp"
#include "util/util.hpp"
#include "widgets/part_preview.hpp"
#include "widgets/pool_browser_part.hpp"
#include "widgets/pool_browser_parametric.hpp"

namespace horizon {

static void header_fun(Gtk::ListBoxRow *row, Gtk::ListBoxRow *before)
{
    if (before && !row->get_header()) {
        auto ret = Gtk::manage(new Gtk::Separator);
        row->set_header(*ret);
    }
}

class UUIDBox : public Gtk::Box {
public:
    using Gtk::Box::Box;
    UUID uuid;
};

PartBrowserWindow::PartBrowserWindow(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &x,
                                     const std::string &pool_path, std::deque<UUID> &favs)
    : Gtk::Window(cobject), pool(pool_path), pool_parametric(pool_path), favorites(favs),
      state_store(this, "part-browser")
{
    x->get_widget("notebook", notebook);
    x->get_widget("menu1", add_search_menu);
    x->get_widget("place_part_button", place_part_button);
    x->get_widget("assign_part_button", assign_part_button);
    x->get_widget("fav_button", fav_button);
    x->get_widget("lb_favorites", lb_favorites);
    x->get_widget("lb_recent", lb_recent);
    x->get_widget("paned", paned);

    lb_favorites->set_header_func(sigc::ptr_fun(header_fun));
    lb_recent->set_header_func(sigc::ptr_fun(header_fun));
    lb_favorites->signal_row_selected().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_favorites_selected));
    lb_favorites->signal_row_activated().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_favorites_activated));
    lb_recent->signal_row_selected().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_favorites_selected));
    lb_recent->signal_row_activated().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_favorites_activated));

    {
        auto la = Gtk::manage(new Gtk::MenuItem("MPN Search"));
        la->signal_activate().connect([this] { add_search(); });
        la->show();
        add_search_menu->append(*la);
    }
    for (const auto &it : pool_parametric.get_tables()) {
        auto la = Gtk::manage(new Gtk::MenuItem(it.second.display_name));
        std::string table_name = it.first;
        la->signal_activate().connect([this, table_name] { add_search_parametric(table_name); });
        la->show();
        add_search_menu->append(*la);
    }
    notebook->signal_switch_page().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_switch_page));
    fav_toggled_conn =
            fav_button->signal_toggled().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_fav_toggled));
    place_part_button->signal_clicked().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_place_part));
    assign_part_button->signal_clicked().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_assign_part));

    preview = Gtk::manage(new PartPreview(pool, false));
    paned->add2(*preview);
    preview->show();

    update_part_current();
    update_favorites();

    add_search();
    for (const auto &it : pool_parametric.get_tables()) {
        add_search_parametric(it.first);
    }
}

void PartBrowserWindow::handle_favorites_selected(Gtk::ListBoxRow *row)
{
    update_part_current();
}

void PartBrowserWindow::handle_favorites_activated(Gtk::ListBoxRow *row)
{
    handle_place_part();
}

void PartBrowserWindow::handle_place_part()
{
    if (part_current)
        s_signal_place_part.emit(part_current);
}

void PartBrowserWindow::handle_assign_part()
{
    if (part_current)
        s_signal_assign_part.emit(part_current);
}

void PartBrowserWindow::handle_fav_toggled()
{
    std::cout << "fav toggled" << std::endl;
    if (part_current) {
        if (fav_button->get_active()) {
            assert(std::count(favorites.begin(), favorites.end(), part_current) == 0);
            favorites.push_front(part_current);
        }
        else {
            assert(std::count(favorites.begin(), favorites.end(), part_current) == 1);
            auto it = std::find(favorites.begin(), favorites.end(), part_current);
            favorites.erase(it);
        }
        update_favorites();
    }
}

void PartBrowserWindow::handle_switch_page(Gtk::Widget *w, guint index)
{
    update_part_current();
}

void PartBrowserWindow::placed_part(const UUID &uu)
{
    auto ncount = std::count(recents.begin(), recents.end(), uu);
    assert(ncount < 2);
    if (ncount) {
        auto it = std::find(recents.begin(), recents.end(), uu);
        recents.erase(it);
    }
    recents.push_front(uu);
    update_recents();
}

void PartBrowserWindow::go_to_part(const UUID &uu)
{
    auto page = notebook->get_nth_page(notebook->get_current_page());
    auto br = dynamic_cast<PoolBrowserPart *>(page);
    if (br)
        br->go_to(uu);
    else
        add_search(uu);
}

void PartBrowserWindow::update_favorites()
{
    auto children = lb_favorites->get_children();
    for (auto it : children) {
        lb_favorites->remove(*it);
    }

    for (const auto &it : favorites) {
        const Part *part = nullptr;
        try {
            part = pool.get_part(it);
        }
        catch (const std::runtime_error &e) {
            part = nullptr;
        }
        if (part) {
            auto box = Gtk::manage(new UUIDBox(Gtk::ORIENTATION_VERTICAL, 4));
            box->uuid = it;
            auto la_MPN = Gtk::manage(new Gtk::Label());
            la_MPN->set_xalign(0);
            la_MPN->set_markup("<b>" + part->get_MPN() + "</b>");
            box->pack_start(*la_MPN, false, false, 0);

            auto la_mfr = Gtk::manage(new Gtk::Label());
            la_mfr->set_xalign(0);
            la_mfr->set_text(part->get_manufacturer());
            box->pack_start(*la_mfr, false, false, 0);

            box->set_margin_top(4);
            box->set_margin_bottom(4);
            box->set_margin_start(4);
            box->set_margin_end(4);
            lb_favorites->append(*box);
            box->show_all();
        }
    }
}

void PartBrowserWindow::update_recents()
{
    auto children = lb_recent->get_children();
    for (auto it : children) {
        lb_recent->remove(*it);
    }

    for (const auto &it : recents) {
        auto part = pool.get_part(it);
        if (part) {
            auto box = Gtk::manage(new UUIDBox(Gtk::ORIENTATION_VERTICAL, 4));
            box->uuid = it;
            auto la_MPN = Gtk::manage(new Gtk::Label());
            la_MPN->set_xalign(0);
            la_MPN->set_markup("<b>" + part->get_MPN() + "</b>");
            box->pack_start(*la_MPN, false, false, 0);

            auto la_mfr = Gtk::manage(new Gtk::Label());
            la_mfr->set_xalign(0);
            la_mfr->set_text(part->get_manufacturer());
            box->pack_start(*la_mfr, false, false, 0);

            box->set_margin_top(4);
            box->set_margin_bottom(4);
            box->set_margin_start(4);
            box->set_margin_end(4);
            lb_recent->append(*box);
            box->show_all();
        }
    }
}

void PartBrowserWindow::update_part_current()
{
    if (in_destruction())
        return;
    auto page = notebook->get_nth_page(notebook->get_current_page());
    SelectionProvider *prv = nullptr;
    prv = dynamic_cast<SelectionProvider *>(page);

    if (prv) {
        part_current = prv->get_selected();
    }
    else {
        if (page->get_name() == "fav") {
            auto row = lb_favorites->get_selected_row();
            if (row) {
                part_current = dynamic_cast<UUIDBox *>(row->get_child())->uuid;
            }
            else {
                part_current = UUID();
            }
        }
        else if (page->get_name() == "recent") {
            auto row = lb_recent->get_selected_row();
            if (row) {
                part_current = dynamic_cast<UUIDBox *>(row->get_child())->uuid;
            }
            else {
                part_current = UUID();
            }
        }
        else {
            part_current = UUID();
        }
    }
    auto ncount = std::count(favorites.begin(), favorites.end(), part_current);
    assert(ncount < 2);
    fav_toggled_conn.block();
    fav_button->set_active(ncount > 0);
    fav_toggled_conn.unblock();

    place_part_button->set_sensitive(part_current);
    assign_part_button->set_sensitive(part_current && can_assign);
    fav_button->set_sensitive(part_current);
    if (part_current) {
        preview->load(pool.get_part(part_current));
    }
    else {
        preview->load(nullptr);
    }
}

void PartBrowserWindow::set_can_assign(bool v)
{
    can_assign = v;
    assign_part_button->set_sensitive(part_current && can_assign);
}

void PartBrowserWindow::add_search(const UUID &part)
{
    auto ch = Gtk::manage(new PoolBrowserPart(&pool));
    ch->get_style_context()->add_class("background");
    auto tab_label = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
    auto la = Gtk::manage(new Gtk::Label("MPN Search"));
    auto close_button = Gtk::manage(new Gtk::Button());
    close_button->set_relief(Gtk::RELIEF_NONE);
    close_button->set_image_from_icon_name("window-close-symbolic");
    close_button->signal_clicked().connect([this, ch] { notebook->remove(*ch); });
    tab_label->pack_start(*close_button, false, false, 0);
    tab_label->pack_start(*la, true, true, 0);
    ch->show_all();
    tab_label->show_all();
    auto index = notebook->append_page(*ch, *tab_label);
    notebook->set_current_page(index);

    search_views.insert(ch);
    ch->signal_selected().connect(sigc::mem_fun(*this, &PartBrowserWindow::update_part_current));
    ch->signal_activated().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_place_part));
    if (part)
        ch->go_to(part);
}

void PartBrowserWindow::add_search_parametric(const std::string &table_name)
{
    auto ch = Gtk::manage(new PoolBrowserParametric(&pool, &pool_parametric, table_name));
    ch->get_style_context()->add_class("background");
    auto tab_label = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
    auto la = Gtk::manage(new Gtk::Label(pool_parametric.get_tables().at(table_name).display_name));
    auto close_button = Gtk::manage(new Gtk::Button());
    close_button->set_relief(Gtk::RELIEF_NONE);
    close_button->set_image_from_icon_name("window-close-symbolic");
    close_button->signal_clicked().connect([this, ch] { notebook->remove(*ch); });
    tab_label->pack_start(*close_button, false, false, 0);
    tab_label->pack_start(*la, true, true, 0);
    ch->show_all();
    tab_label->show_all();
    auto index = notebook->append_page(*ch, *tab_label);
    notebook->set_current_page(index);

    search_views.insert(ch);
    ch->signal_selected().connect(sigc::mem_fun(*this, &PartBrowserWindow::update_part_current));
    ch->signal_activated().connect(sigc::mem_fun(*this, &PartBrowserWindow::handle_place_part));
}

PartBrowserWindow *PartBrowserWindow::create(Gtk::Window *p, const std::string &pool_path, std::deque<UUID> &favs)
{
    PartBrowserWindow *w;
    Glib::RefPtr<Gtk::Builder> x = Gtk::Builder::create();
    x->add_from_resource(
            "/net/carrotIndustries/horizon/pool-prj-mgr/prj-mgr/part_browser/"
            "part_browser.ui");
    x->get_widget_derived("window", w, pool_path, favs);

    return w;
}
} // namespace horizon
