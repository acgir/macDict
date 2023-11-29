#include "LineEdit.h"
#include <QKeyEvent>

LineEdit::LineEdit(const std::string &text, QWidget *parent)
	: QLineEdit(QString::fromUtf8(text.c_str()), parent) {}

LineEdit::~LineEdit() {}

void LineEdit::keyPressEvent(QKeyEvent *event) {

	const bool ctrl = (event->modifiers() & Qt::ControlModifier);
	const bool alt  = (event->modifiers() & Qt::AltModifier);

	if (event->key() == Qt::Key_Backspace && alt) {
		clear();

	} else if (event->key() == Qt::Key_F && ctrl) {
		cursorForward(false);

	} else if (event->key() == Qt::Key_B && ctrl) {
		cursorBackward(false);

	} else if (event->key() == Qt::Key_F && alt) {
		cursorWordForward(false);

	} else if (event->key() == Qt::Key_B && alt) {
		cursorWordBackward(false);

	} else {
		QLineEdit::keyPressEvent(event);
	}
}
