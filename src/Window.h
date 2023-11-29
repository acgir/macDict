#ifndef INCLUDED_WINDOW_H
#define INCLUDED_WINDOW_H

#include <QtWidgets/QMainWindow>

QT_FORWARD_DECLARE_CLASS(QListWidget);
QT_FORWARD_DECLARE_CLASS(QListWidgetItem);
QT_FORWARD_DECLARE_CLASS(QSplitter);
QT_FORWARD_DECLARE_CLASS(QScrollArea);
QT_FORWARD_DECLARE_CLASS(QWebEngineView);
QT_FORWARD_DECLARE_CLASS(QPushButton);
QT_FORWARD_DECLARE_CLASS(QLabel);

class LineEdit;
struct DictionaryRef;

class Window : public QMainWindow {
Q_OBJECT
public:
	Window(const DictionaryRef &dict,
	       const bool dark,
	       const std::string &word,
	       QWidget *parent = NULL);
	virtual ~Window();

protected:
	virtual void keyPressEvent(QKeyEvent *event);
	virtual void closeEvent(QCloseEvent *event);
	virtual void showEvent(QShowEvent *event);

private slots:
	void slot_text_changed(const QString &);
	void slot_item_changed(QListWidgetItem *cur, QListWidgetItem *prev);
	void slot_toggle_theme(bool);
	void slot_text_small(bool);
	void slot_text_big(bool);

private:
	const DictionaryRef &_dict;
	bool _dark;

	QListWidget *_list;
	QSplitter *_split;
	QScrollArea *_scroll;
	QWebEngineView *_view;
	QWidget *_left;
	QWidget *_right;
	QWidget *_top;
	LineEdit *_line;
	// QPushButton *_back;
	// QPushButton *_fwd;
	QPushButton *_small;
	QPushButton *_big;
	QPushButton *_theme;
	QLabel *_found;

	void set_zoom(double zoom);
	void update_definition(const bool from_field);
	void definition_of_list_item(std::ostringstream &out) const;
	void begin_html(std::ostream &out) const;
	void end_html(std::ostream &out) const;
	void update_list_theme();
};

#endif
