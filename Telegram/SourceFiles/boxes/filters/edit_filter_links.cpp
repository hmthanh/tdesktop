/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_links.h"

#include "apiwrap.h"
#include "boxes/peers/edit_peer_invite_link.h" // InviteLinkQrBox.
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/controls/invite_link_label.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <xxhash.h>

namespace {

constexpr auto kMaxLinkTitleLength = 32;

using InviteLinkData = Data::ChatFilterLink;
class Row;

enum class Color {
	Permanent,

	Count,
};

struct InviteLinkAction {
	enum class Type {
		Copy,
		Share,
		Edit,
		Delete,
	};
	QString link;
	Type type = Type::Copy;
};

struct Errors {
	QString status;
	QString toast;
};

[[nodiscard]] std::optional<Errors> ErrorForSharing(
		not_null<History*> history) {
	const auto result = [](const QString &status, const QString &toast) {
		return Errors{ status, toast };
	};
	const auto peer = history->peer;
	if (const auto user = peer->asUser()) {
		return user->isBot()
			? result(
				tr::lng_filters_link_bot_status(tr::now),
				tr::lng_filters_link_bot_error(tr::now))
			: result(
				tr::lng_filters_link_private_status(tr::now),
				tr::lng_filters_link_private_error(tr::now));
	} else if (const auto chat = history->peer->asChat()) {
		if (!chat->canHaveInviteLink()) {
			return result(
				tr::lng_filters_link_noadmin_status(tr::now),
				tr::lng_filters_link_noadmin_group_error(tr::now));
		}
		return std::nullopt;
	} else if (const auto channel = history->peer->asChannel()) {
		if (!channel->canHaveInviteLink()) {
			return result(
				tr::lng_filters_link_noadmin_status(tr::now),
				(channel->isMegagroup()
					? tr::lng_filters_link_noadmin_group_error(tr::now)
					: tr::lng_filters_link_noadmin_channel_error(tr::now)));
		}
		return std::nullopt;
	}
	Unexpected("Peer type in ErrorForSharing.");
}

void ShowEmptyLinkError(not_null<Window::SessionController*> window) {
	Ui::ShowMultilineToast({
		.parentOverride = Window::Show(window).toastParent(),
		.text = { tr::lng_filters_empty(tr::now) },
	});
}

void ChatFilterLinkBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Data::ChatFilterLink data) {
	using namespace rpl::mappers;

	const auto link = data.url;
	box->setTitle(tr::lng_group_invite_edit_title());

	const auto container = box->verticalLayout();
	const auto addTitle = [&](
			not_null<Ui::VerticalLayout*> container,
			rpl::producer<QString> text) {
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::settingsSubsectionTitle),
			(st::settingsSubsectionTitlePadding
				+ style::margins(0, st::settingsSectionSkip, 0, 0)));
	};
	const auto addDivider = [&](
			not_null<Ui::VerticalLayout*> container,
			rpl::producer<QString> text,
			style::margins margins = style::margins()) {
		container->add(
			object_ptr<Ui::DividerLabel>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					std::move(text),
					st::boxDividerLabel),
				st::settingsDividerLabelPadding),
			margins);
	};

	struct State {
	};
	const auto state = box->lifetime().make_state<State>(State{
	});

	const auto labelField = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_group_invite_label_header(),
			data.title),
		style::margins(
			st::settingsSubsectionTitlePadding.left(),
			st::settingsSectionSkip,
			st::settingsSubsectionTitlePadding.right(),
			st::settingsSectionSkip * 2));
	labelField->setMaxLength(kMaxLinkTitleLength);
	Settings::AddDivider(container);

	box->setFocusCallback([=] {
		labelField->setFocusFast();
	});

	const auto &saveLabel = link.isEmpty()
		? tr::lng_formatting_link_create
		: tr::lng_settings_save;
	box->addButton(saveLabel(), [=] {
		session->data().chatsFilters().edit(
			data.id,
			data.url,
			labelField->getLastText().trimmed());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

class RowDelegate {
public:
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
	virtual void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) = 0;
};

class Row final : public PeerListRow {
public:
	Row(not_null<RowDelegate*> delegate, const InviteLinkData &data);

	void update(const InviteLinkData &data);

	[[nodiscard]] InviteLinkData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	const not_null<RowDelegate*> _delegate;
	InviteLinkData _data;
	QString _status;
	Color _color = Color::Permanent;

};

[[nodiscard]] uint64 ComputeRowId(const QString &link) {
	return XXH64(link.data(), link.size() * sizeof(ushort), 0);
}

[[nodiscard]] uint64 ComputeRowId(const InviteLinkData &data) {
	return ComputeRowId(data.url);
}

[[nodiscard]] Color ComputeColor(const InviteLinkData &link) {
	return Color::Permanent;
}

[[nodiscard]] QString ComputeStatus(const InviteLinkData &link) {
	return tr::lng_filters_chats_count(tr::now, lt_count, link.chats.size());
}

Row::Row(not_null<RowDelegate*> delegate, const InviteLinkData &data)
: PeerListRow(ComputeRowId(data))
, _delegate(delegate)
, _data(data)
, _color(ComputeColor(data)) {
	setCustomStatus(ComputeStatus(data));
}

void Row::update(const InviteLinkData &data) {
	_data = data;
	_color = ComputeColor(data);
	setCustomStatus(ComputeStatus(data));
	refreshName(st::inviteLinkList.item);
	_delegate->rowUpdateRow(this);
}

InviteLinkData Row::data() const {
	return _data;
}

QString Row::generateName() {
	if (!_data.title.isEmpty()) {
		return _data.title;
	}
	auto result = _data.url;
	return result.replace(
		u"https://"_q,
		QString()
	).replace(
		u"t.me/+"_q,
		QString()
	).replace(
		u"t.me/joinchat/"_q,
		QString()
	);
}

QString Row::generateShortName() {
	return generateName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(bool forceRound) {
	return [=](
			QPainter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		_delegate->rowPaintIcon(p, x, y, size, _color);
	};
}

QSize Row::rightActionSize() const {
	return QSize(
		st::inviteLinkThreeDotsIcon.width(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins Row::rightActionMargins() const {
	return QMargins(
		0,
		(st::inviteLinkList.item.height - rightActionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void Row::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	(actionSelected
		? st::inviteLinkThreeDotsIconOver
		: st::inviteLinkThreeDotsIcon).paint(p, x, y, outerWidth);
}

class LinksController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	LinksController(
		not_null<Window::SessionController*> window,
		rpl::producer<std::vector<InviteLinkData>> content,
		Fn<Data::ChatFilter()> currentFilter);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void rowUpdateRow(not_null<Row*> row) override;
	void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) override;

private:
	void appendRow(const InviteLinkData &data);
	bool removeRow(const QString &link);

	void rebuild(const std::vector<InviteLinkData> &rows);

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);

	const not_null<Window::SessionController*> _window;
	Fn<Data::ChatFilter()> _currentFilter;
	rpl::variable<std::vector<InviteLinkData>> _rows;
	base::unique_qptr<Ui::PopupMenu> _menu;

	std::array<QImage, int(Color::Count)> _icons;
	rpl::lifetime _lifetime;

};

class LinkController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	LinkController(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		InviteLinkData data);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void showFinished() override;

	[[nodiscard]] rpl::producer<bool> hasChangesValue() const;
	[[nodiscard]] base::flat_set<not_null<PeerData*>> selected() const;

private:
	void setupAboveWidget();
	void setupBelowWidget();
	void addHeader(not_null<Ui::VerticalLayout*> container);
	void addLinkBlock(not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionController*> _window;
	InviteLinkData _data;

	QString _filterTitle;
	base::flat_set<not_null<History*>> _filterChats;
	base::flat_map<not_null<PeerData*>, QString> _denied;
	rpl::variable<base::flat_set<not_null<PeerData*>>> _selected;
	base::flat_set<not_null<PeerData*>> _initial;

	base::unique_qptr<Ui::PopupMenu> _menu;

	QString _link;

	rpl::variable<bool> _hasChanges = false;

	rpl::event_stream<> _showFinished;

	rpl::lifetime _lifetime;

};

LinkController::LinkController(
	not_null<Window::SessionController*> window,
	const Data::ChatFilter &filter,
	InviteLinkData data)
: _window(window)
, _filterTitle(filter.title())
, _filterChats(filter.always()) {
	_data = std::move(data);
	_link = _data.url;
}

void LinkController::addHeader(not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
		container.get());
	const auto verticalLayout = container->add(
		object_ptr<Ui::VerticalLayout>(container.get()));

	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = u"filters"_q,
			.sizeOverride = {
				st::settingsFilterIconSize,
				st::settingsFilterIconSize,
			},
		},
		st::settingsFilterIconPadding);
	_showFinished.events(
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, verticalLayout->lifetime());
	verticalLayout->add(std::move(icon.widget));

	verticalLayout->add(
		object_ptr<Ui::CenterWrap<>>(
			verticalLayout,
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				(_data.url.isEmpty()
					? tr::lng_filters_link_no_about(Ui::Text::WithEntities)
					: tr::lng_filters_link_share_about(
						lt_folder,
						rpl::single(Ui::Text::Bold(_filterTitle)),
						Ui::Text::WithEntities)),
				st::settingsFilterDividerLabel)),
		st::settingsFilterDividerLabelPadding);

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

object_ptr<Ui::BoxContent> DeleteLinkBox(
		not_null<Window::SessionController*> window,
		const InviteLinkData &link) {
	const auto sure = [=](Fn<void()> &&close) {
		window->session().data().chatsFilters().destroy(link.id, link.url);
		close();
	};
	return Ui::MakeConfirmBox({
		.text = tr::lng_filters_link_delete_sure(tr::now),
		.confirmed = sure,
		.confirmText = tr::lng_box_delete(tr::now),
	});
}

void LinkController::addLinkBlock(not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto link = _data.url;
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		CopyInviteLink(delegate()->peerListToastParent(), link);
	});
	const auto shareLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			ShareInviteLinkBox(&_window->session(), link),
			Ui::LayerOption::KeepOther);
	});
	const auto getLinkQr = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			InviteLinkQrBox(link),
			Ui::LayerOption::KeepOther);
	});
	const auto editLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			Box(ChatFilterLinkBox, &_window->session(), _data),
			Ui::LayerOption::KeepOther);
	});
	const auto deleteLink = crl::guard(weak, [=] {
		delegate()->peerListShowBox(
			DeleteLinkBox(_window, _data),
			Ui::LayerOption::KeepOther);
	});

	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(
			container,
			st::popupMenuWithIcons);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink,
			&st::menuIconCopy);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink,
			&st::menuIconShare);
		result->addAction(
			tr::lng_group_invite_context_qr(tr::now),
			getLinkQr,
			&st::menuIconQrCode);
		result->addAction(
			tr::lng_filters_link_name_it(tr::now),
			editLink,
			&st::menuIconEdit);
		result->addAction(
			tr::lng_group_invite_context_delete(tr::now),
			deleteLink,
			&st::menuIconDelete);
		return result;
	};
	AddSubsectionTitle(container, tr::lng_filters_link_subtitle());

	const auto prefix = u"https://"_q;
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		rpl::single(link.startsWith(prefix)
			? link.mid(prefix.size())
			: link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	AddCopyShareLinkButtons(container, copyLink, shareLink);

	AddSkip(container, st::inviteLinkJoinedRowPadding.bottom() * 2);

	AddSkip(container);

	AddDivider(container);
}

void LinkController::prepare() {
	Expects(!_data.url.isEmpty() || _data.chats.empty());

	setupAboveWidget();
	setupBelowWidget();
	for (const auto &history : _data.chats) {
		const auto peer = history->peer;
		auto row = std::make_unique<PeerListRow>(peer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListSetRowChecked(raw, true);
		_initial.emplace(peer);
	}
	for (const auto &history : _filterChats) {
		if (delegate()->peerListFindRow(history->peer->id.value)) {
			continue;
		}
		const auto peer = history->peer;
		auto row = std::make_unique<PeerListRow>(peer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (const auto error = ErrorForSharing(history)) {
			raw->setCustomStatus(error->status);
			_denied.emplace(peer, error->toast);
		} else if (_data.url.isEmpty()) {
			_denied.emplace(peer);
		}
	}
	delegate()->peerListRefreshRows();
	_selected = _initial;
}

void LinkController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	if (const auto i = _denied.find(peer); i != end(_denied)) {
		if (!i->second.isEmpty()) {
			Ui::ShowMultilineToast({
				.parentOverride = delegate()->peerListToastParent(),
				.text = { i->second },
			});
		}
	} else {
		const auto checked = row->checked();
		auto selected = _selected.current();
		delegate()->peerListSetRowChecked(row, !checked);
		if (checked) {
			selected.remove(peer);
		} else {
			selected.emplace(peer);
		}
		const auto has = (_initial != selected);
		_selected = std::move(selected);
		_hasChanges = has;
	}
}

void LinkController::showFinished() {
	_showFinished.fire({});
}

void LinkController::setupAboveWidget() {
	using namespace Settings;

	auto wrap = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = wrap.data();

	addHeader(container);
	if (!_data.url.isEmpty()) {
		addLinkBlock(container);
	}

	auto subtitle = _selected.value(
	) | rpl::map([=](const base::flat_set<not_null<PeerData*>> &selected) {
		return _data.url.isEmpty()
			? tr::lng_filters_link_chats_no(tr::now)
			: selected.empty()
			? tr::lng_filters_link_chats_none(tr::now)
			: tr::lng_filters_link_chats(
				tr::now,
				lt_count,
				float64(selected.size()));
	});
	Settings::AddSubsectionTitle(
		container,
		std::move(subtitle));

	delegate()->peerListSetAboveWidget(std::move(wrap));
}

void LinkController::setupBelowWidget() {
	delegate()->peerListSetBelowWidget(
		object_ptr<Ui::DividerLabel>(
			(QWidget*)nullptr,
			object_ptr<Ui::FlatLabel>(
				(QWidget*)nullptr,
				(_data.url.isEmpty()
					? tr::lng_filters_link_chats_no_about()
					: tr::lng_filters_link_chats_about()),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding));
}

Main::Session &LinkController::session() const {
	return _window->session();
}

rpl::producer<bool> LinkController::hasChangesValue() const {
	return _hasChanges.value();
}

base::flat_set<not_null<PeerData*>> LinkController::selected() const {
	return _selected.current();
}

LinksController::LinksController(
	not_null<Window::SessionController*> window,
	rpl::producer<std::vector<InviteLinkData>> content,
	Fn<Data::ChatFilter()> currentFilter)
: _window(window)
, _currentFilter(std::move(currentFilter))
, _rows(std::move(content)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &image : _icons) {
			image = QImage();
		}
	}, _lifetime);
}

void LinksController::prepare() {
	_rows.value(
	) | rpl::start_with_next([=](const std::vector<InviteLinkData> &rows) {
		rebuild(rows);
	}, _lifetime);
}

void LinksController::rebuild(const std::vector<InviteLinkData> &rows) {
	auto i = 0;
	auto count = delegate()->peerListFullRowsCount();
	while (i < rows.size()) {
		if (i < count) {
			const auto row = delegate()->peerListRowAt(i);
			static_cast<Row*>(row.get())->update(rows[i]);
		} else {
			appendRow(rows[i]);
		}
		++i;
	}
	while (i < count) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(i));
		--count;
	}
	delegate()->peerListRefreshRows();
}

void LinksController::rowClicked(not_null<PeerListRow*> row) {
	const auto link = static_cast<Row*>(row.get())->data();
	delegate()->peerListShowBox(
		ShowLinkBox(_window, _currentFilter(), link),
		Ui::LayerOption::KeepOther);
}

void LinksController::rowRightActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

base::unique_qptr<Ui::PopupMenu> LinksController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = createRowContextMenu(parent, row);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

base::unique_qptr<Ui::PopupMenu> LinksController::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto real = static_cast<Row*>(row.get());
	const auto data = real->data();
	const auto link = data.url;
	const auto copyLink = [=] {
		CopyInviteLink(delegate()->peerListToastParent(), link);
	};
	const auto shareLink = [=] {
		delegate()->peerListShowBox(
			ShareInviteLinkBox(&_window->session(), link),
			Ui::LayerOption::KeepOther);
	};
	const auto getLinkQr = [=] {
		delegate()->peerListShowBox(
			InviteLinkQrBox(link),
			Ui::LayerOption::KeepOther);
	};
	const auto editLink = [=] {
		delegate()->peerListShowBox(
			Box(ChatFilterLinkBox, &_window->session(), data),
			Ui::LayerOption::KeepOther);
	};
	const auto deleteLink = [=] {
		delegate()->peerListShowBox(
			DeleteLinkBox(_window, data),
			Ui::LayerOption::KeepOther);
	};
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(
		tr::lng_group_invite_context_copy(tr::now),
		copyLink,
		&st::menuIconCopy);
	result->addAction(
		tr::lng_group_invite_context_share(tr::now),
		shareLink,
		&st::menuIconShare);
	result->addAction(
		tr::lng_group_invite_context_qr(tr::now),
		getLinkQr,
		&st::menuIconQrCode);
	result->addAction(
		tr::lng_filters_link_name_it(tr::now),
		editLink,
		&st::menuIconEdit);
	result->addAction(
		tr::lng_group_invite_context_delete(tr::now),
		deleteLink,
		&st::menuIconDelete);
	return result;
}

Main::Session &LinksController::session() const {
	return _window->session();
}

void LinksController::appendRow(const InviteLinkData &data) {
	delegate()->peerListAppendRow(std::make_unique<Row>(this, data));
}

bool LinksController::removeRow(const QString &link) {
	if (const auto row = delegate()->peerListFindRow(ComputeRowId(link))) {
		delegate()->peerListRemoveRow(row);
		return true;
	}
	return false;
}

void LinksController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void LinksController::rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		Color color) {
	const auto skip = st::inviteLinkIconSkip;
	const auto inner = size - 2 * skip;
	const auto bg = [&] {
		switch (color) {
		case Color::Permanent: return &st::msgFile1Bg;
		}
		Unexpected("Color in LinksController::rowPaintIcon.");
	}();
	const auto stroke = st::inviteLinkIconStroke;
	auto &icon = _icons[int(color)];
	if (icon.isNull()) {
		icon = QImage(
			QSize(inner, inner) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		icon.fill(Qt::transparent);
		icon.setDevicePixelRatio(style::DevicePixelRatio());

		auto p = QPainter(&icon);
		p.setPen(Qt::NoPen);
		p.setBrush(*bg);
		{
			auto hq = PainterHighQualityEnabler(p);
			p.drawEllipse(QRect(0, 0, inner, inner));
		}
		st::inviteLinkIcon.paintInCenter(p, { 0, 0, inner, inner });
	}
	p.drawImage(x + skip, y + skip, icon);
}

class LinkChatsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	LinkChatsController(
		not_null<Window::SessionController*> controller,
		FilterId id,
		const InviteLinkData &data);
	~LinkChatsController();

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const not_null<Window::SessionController*> _controller;
	const FilterId _id = 0;
	InviteLinkData _data;

};

LinkChatsController::LinkChatsController(
	not_null<Window::SessionController*> controller,
	FilterId id,
	const InviteLinkData &data)
: _controller(controller)
, _id(id)
, _data(data) {
}

LinkChatsController::~LinkChatsController() = default;

void LinkChatsController::prepare() {
	for (const auto &history : _data.chats) {
		delegate()->peerListAppendRow(
			std::make_unique<PeerListRow>(history->peer));
	}
	delegate()->peerListRefreshRows();
}

void LinkChatsController::rowClicked(not_null<PeerListRow*> row) {
}

Main::Session &LinkChatsController::session() const {
	return _controller->session();
}

} // namespace

std::vector<not_null<PeerData*>> CollectFilterLinkChats(
		const Data::ChatFilter &filter) {
	return filter.always() | ranges::views::filter([](
			not_null<History*> history) {
		return !ErrorForSharing(history);
	}) | ranges::views::transform(&History::peer) | ranges::to_vector;
}

bool GoodForExportFilterLink(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter) {
	using Flag = Data::ChatFilter::Flag;
	if (!filter.never().empty() || (filter.flags() & ~Flag::Community)) {
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(window).toastParent(),
			.text = { tr::lng_filters_link_cant(tr::now) },
		});
		return false;
	}
	return true;
}

void ExportFilterLink(
		FilterId id,
		const std::vector<not_null<PeerData*>> &peers,
		Fn<void(Data::ChatFilterLink)> done) {
	Expects(!peers.empty());

	const auto front = peers.front();
	const auto session = &front->session();
	auto mtpPeers = peers | ranges::views::transform(
		[](not_null<PeerData*> peer) { return MTPInputPeer(peer->input); }
	) | ranges::to<QVector>();
	session->api().request(MTPcommunities_ExportCommunityInvite(
		MTP_inputCommunityDialogFilter(MTP_int(id)),
		MTP_string(), // title
		MTP_vector<MTPInputPeer>(std::move(mtpPeers))
	)).done([=](const MTPcommunities_ExportedCommunityInvite &result) {
		const auto &data = result.data();
		session->data().chatsFilters().apply(MTP_updateDialogFilter(
			MTP_flags(MTPDupdateDialogFilter::Flag::f_filter),
			MTP_int(id),
			data.vfilter()));
		const auto link = session->data().chatsFilters().add(
			id,
			data.vinvite());
		done(link);
	}).fail([=](const MTP::Error &error) {
		done({ .id = id });
	}).send();
}

void EditLinkChats(
		const Data::ChatFilterLink &link,
		base::flat_set<not_null<PeerData*>> peers) {
	Expects(!peers.empty());
	Expects(link.id != 0);
	Expects(!link.url.isEmpty());

	const auto id = link.id;
	const auto front = peers.front();
	const auto session = &front->session();
	auto mtpPeers = peers | ranges::views::transform(
		[](not_null<PeerData*> peer) { return MTPInputPeer(peer->input); }
	) | ranges::to<QVector>();
	session->api().request(MTPcommunities_EditExportedInvite(
		MTP_flags(MTPcommunities_EditExportedInvite::Flag::f_peers),
		MTP_inputCommunityDialogFilter(MTP_int(link.id)),
		MTP_string(link.url),
		MTPstring(), // title
		MTP_vector<MTPInputPeer>(std::move(mtpPeers))
	)).done([=](const MTPExportedCommunityInvite &result) {
		const auto &data = result.data();
		const auto link = session->data().chatsFilters().add(id, result);
		//done(link);
	}).fail([=](const MTP::Error &error) {
		//done({ .id = id });
	}).send();
}

object_ptr<Ui::BoxContent> ShowLinkBox(
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		const Data::ChatFilterLink &link) {
	auto controller = std::make_unique<LinkController>(window, filter, link);
	const auto raw = controller.get();
	auto initBox = [=](not_null<Ui::BoxContent*> box) {
		box->setTitle(!link.title.isEmpty()
			? rpl::single(link.title)
			: tr::lng_filters_link_title());

		raw->hasChangesValue(
		) | rpl::start_with_next([=](bool has) {
			box->clearButtons();
			if (has) {
				box->addButton(tr::lng_settings_save(), [=] {
					const auto chosen = raw->selected();
					if (chosen.empty()) {
						ShowEmptyLinkError(window);
					} else {
						EditLinkChats(link, chosen);
					}
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			} else {
				box->addButton(tr::lng_about_done(), [=] {
					box->closeBox();
				});
			}
		}, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), std::move(initBox));
}

void SetupFilterLinks(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> window,
		rpl::producer<std::vector<Data::ChatFilterLink>> value,
		Fn<Data::ChatFilter()> currentFilter) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<PeerListContentDelegateShow>(
		std::make_shared<Window::Show>(window));
	const auto controller = lifetime.make_state<LinksController>(
		window,
		std::move(value),
		std::move(currentFilter));
	controller->setStyleOverrides(&st::inviteLinkList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);
}