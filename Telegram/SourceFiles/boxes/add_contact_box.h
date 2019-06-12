/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "styles/style_widgets.h"

class ConfirmBox;
class PeerListBox;

constexpr auto kMaxBioLength = 70;

style::InputField CreateBioFieldStyle();

namespace Ui {
class FlatLabel;
class InputField;
class PhoneInput;
class UsernameInput;
class Checkbox;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class LinkButton;
class UserpicButton;
} // namespace Ui

enum class PeerFloodType {
	Send,
	InviteGroup,
	InviteChannel,
};
QString PeerFloodErrorText(PeerFloodType type);
void ShowAddParticipantsError(
	const QString &error,
	not_null<PeerData*> chat,
	const std::vector<not_null<UserData*>> &users);

class AddContactBox : public BoxContent, public RPCSender {
public:
	AddContactBox(QWidget*, QString fname = QString(), QString lname = QString(), QString phone = QString());
	AddContactBox(QWidget*, UserData *user);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setInnerFocus() override;

private:
	void submit();
	void retry();
	void save();
	void updateButtons();
	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveUserFail(const RPCError &e);

	UserData *_user = nullptr;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;
	object_ptr<Ui::PhoneInput> _phone;

	bool _retrying = false;
	bool _invertOrder = false;

	uint64 _contactId = 0;

	mtpRequestId _addRequest = 0;
	QString _sentName;

};

class GroupInfoBox : public BoxContent, private MTP::Sender {
public:
	enum class Type {
		Group,
		Channel,
		Megagroup,
	};
	GroupInfoBox(
		QWidget*,
		Type type,
		const QString &title = QString(),
		Fn<void(not_null<ChannelData*>)> channelDone = nullptr);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void createChannel(const QString &title, const QString &description);
	void createGroup(not_null<PeerListBox*> selectUsersBox, const QString &title, const std::vector<not_null<PeerData*>> &users);
	void submitName();
	void submit();

	void descriptionResized();
	void updateMaxHeight();

	Type _type = Type::Group;
	QString _initialTitle;
	Fn<void(not_null<ChannelData*>)> _channelDone;

	object_ptr<Ui::UserpicButton> _photo = { nullptr };
	object_ptr<Ui::InputField> _title = { nullptr };
	object_ptr<Ui::InputField> _description = { nullptr };

	// group / channel creation
	mtpRequestId _creationRequestId = 0;
	ChannelData *_createdChannel = nullptr;

};

class SetupChannelBox : public BoxContent, public RPCSender {
public:
	SetupChannelBox(QWidget*, ChannelData *channel, bool existing = false);

	void setInnerFocus() override;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Privacy {
		Public,
		Private,
	};
	void privacyChanged(Privacy value);
	void updateSelected(const QPoint &cursorGlobalPosition);
	void handleChange();
	void check();
	void save();

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	void updateMaxHeight();

	void showRevokePublicLinkBoxForEdit();

	ChannelData *_channel = nullptr;
	bool _existing = false;

	std::shared_ptr<Ui::RadioenumGroup<Privacy>> _privacyGroup;
	object_ptr<Ui::Radioenum<Privacy>> _public;
	object_ptr<Ui::Radioenum<Privacy>> _private;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Ui::Text::String _aboutPublic, _aboutPrivate;

	object_ptr<Ui::UsernameInput> _link;

	QRect _invitationLink;
	bool _linkOver = false;
	bool _tooMuchUsernames = false;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	QTimer _checkTimer;

};

class EditNameBox : public BoxContent, public RPCSender {
public:
	EditNameBox(QWidget*, not_null<UserData*> user);

protected:
	void setInnerFocus() override;
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void submit();
	void save();
	void saveSelfDone(const MTPUser &user);
	bool saveSelfFail(const RPCError &error);

	not_null<UserData*> _user;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;

	bool _invertOrder = false;

	mtpRequestId _requestId = 0;
	QString _sentName;

};

class RevokePublicLinkBox : public BoxContent, public RPCSender {
public:
	RevokePublicLinkBox(QWidget*, Fn<void()> revokeCallback);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _aboutRevoke;

	class Inner;
	QPointer<Inner> _inner;

	int _innerTop = 0;
	Fn<void()> _revokeCallback;

};
