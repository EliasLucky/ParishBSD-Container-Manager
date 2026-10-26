// cli/parish-compartment-dialog.cpp  Qt6 dialog helper.
//
// Invoked by parish-app-launch and any other helper that needs a
// message box from a non-GUI process. Reads argv, renders a
// QMessageBox, returns a numeric button ID on stdout (or a
// non-zero exit on window-manager close).
//
// Exit codes:
//   0   a button was clicked; the numeric ID was printed on stdout
//   1   the window was closed by the window manager (no button clicked)
//   2   invalid arguments (missing --button, malformed --button spec)
//   3   cannot start Qt (no display; typically a headless environment)
//
// The exit codes are a contract: _dialog_choice in parish-app-launch
// treats non-zero as "user cancelled" and falls through to the
// cancel path.

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVariant>
#include <QList>
#include <QAbstractButton>

#include <cstdio>
#include <cstdlib>

namespace {

// Parses a --button value of the form "label:id" into (label, id).
// The id is extracted from the last ':' so a label that itself
// contains ':' still parses ("Open: detailed view:10" -> label
// "Open: detailed view", id 10).
//
// Returns false on malformed input (no ':' or non-integer id). The
// caller refuses with exit 2 in that case.
bool parse_button_spec(const QString& spec, QString& label, int& id)
{
    const int colon = spec.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == spec.size() - 1) {
        return false;
    }
    label = spec.left(colon);
    bool ok = false;
    id = spec.mid(colon + 1).toInt(&ok);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    // QApplication (not QGuiApplication) is required for
    // QMessageBox, which uses widget-based rendering.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("parish-compartment-dialog"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Display a message box for a ParishBSD compartment action."));
    parser.addHelpOption();

    QCommandLineOption optType(
        QStringLiteral("type"),
        QStringLiteral("Dialog type: question, warning, error, or info."),
        QStringLiteral("type"),
        QStringLiteral("info"));
    QCommandLineOption optTitle(
        QStringLiteral("title"),
        QStringLiteral("Window title."),
        QStringLiteral("title"));
    QCommandLineOption optText(
        QStringLiteral("text"),
        QStringLiteral("Body text. Rendered as plain text; HTML is not interpreted."),
        QStringLiteral("text"));
    QCommandLineOption optButton(
        QStringLiteral("button"),
        QStringLiteral("Button spec of the form label:id. Repeatable."),
        QStringLiteral("label:id"));

    parser.addOption(optType);
    parser.addOption(optTitle);
    parser.addOption(optText);
    parser.addOption(optButton);

    // Do not call process() for it exits on parse errors. Use parse() and inspect.
    parser.parse(QCoreApplication::arguments());

    if (QApplication::instance() == nullptr) {
        std::fprintf(stderr,
            "parish-compartment-dialog: cannot start Qt (no display?)\n");
        return 3;
    }

    const QString typeStr = parser.value(optType).toLower();
    QMessageBox::Icon icon = QMessageBox::Information;
    if (typeStr == QLatin1String("question")) {
        icon = QMessageBox::Question;
    } else if (typeStr == QLatin1String("warning")) {
        icon = QMessageBox::Warning;
    } else if (typeStr == QLatin1String("error")) {
        icon = QMessageBox::Critical;
    } else if (typeStr == QLatin1String("info")) {
        icon = QMessageBox::Information;
    } else {
        std::fprintf(stderr,
            "parish-compartment-dialog: unknown --type '%s' "
            "(expected question, warning, error, or info)\n",
            qPrintable(typeStr));
        return 2;
    }

    QMessageBox box;
    box.setIcon(icon);
    box.setWindowTitle(parser.value(optTitle));

    // PlainText is not optional. The --text argument can contain
    // jail-originated strings (app names, error messages derived
    // from metadata). AutoText or RichText would interpret any
    // angle-bracket content as HTML.
    box.setTextFormat(Qt::PlainText);
    box.setText(parser.value(optText));

    QList<QPushButton*> buttons;
    const QStringList buttonSpecs = parser.values(optButton);
    for (const QString& spec : buttonSpecs) {
        QString label;
        int id = 0;
        if (!parse_button_spec(spec, label, id)) {
            std::fprintf(stderr,
                "parish-compartment-dialog: malformed --button '%s' "
                "(expected label:id with a numeric id)\n",
                qPrintable(spec));
            return 2;
        }
        QPushButton* b = box.addButton(label, QMessageBox::AcceptRole);
        // Property lookup is the transport between the button widget
        // and the ID printed on stdout. QVariant<int> so the read
        // is type-safe.
        b->setProperty("parish_button_id", QVariant(id));
        buttons.append(b);
    }

    if (buttons.isEmpty()) {
        // No buttons means the caller made a mistake. Rather than
        // showing a dialog with only the WM close button (which
        // returns null on clickedButton and would exit 1), refuse
        // with a clear error.
        std::fprintf(stderr,
            "parish-compartment-dialog: at least one --button is required\n");
        return 2;
    }

    // The first button is the default (Enter key). This is the
    // "safe" default for a question dialog where the first button
    // is the affirmative action — but note that the caller is
    // responsible for making the first button the safe one. The
    // dialog helper does not impose an ordering.
    box.setDefaultButton(buttons.first());

    box.exec();

    QAbstractButton* clicked = box.clickedButton();
    if (clicked == nullptr) {
        // Window-manager close button. No numeric ID to return.
        // Exit non-zero so the caller falls through to its cancel
        // path. Do NOT print anything on stdout — the caller may
        // be parsing it.
        return 1;
    }

    const QVariant idVar = clicked->property("parish_button_id");
    if (!idVar.isValid()) {
        // Should not happen: we set this property on every button
        // we create. If it does, the dialog has a bug; return a
        // distinguishable exit code.
        std::fprintf(stderr,
            "parish-compartment-dialog: clicked button has no "
            "parish_button_id property (internal error)\n");
        return 3;
    }

    QTextStream out(stdout);
    out << idVar.toInt() << '\n';
    out.flush();
    return 0;
}