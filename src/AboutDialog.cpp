#include "AboutDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <utility>

AboutDialog::AboutDialog(AboutInfo info, QWidget *parent, const QIcon &icon)
    : QDialog(parent),
      m_info(std::move(info)) {
    setWindowTitle("About");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(true);
    setMinimumWidth(480);

    // --- Root layout
    auto *root = new QVBoxLayout(this); // NOLINT(cppcoreguidelines-owning-memory)
    root->setContentsMargins(18, 18, 18, 14);
    root->setSpacing(12);

    // --- Header row (icon + title)
    auto *header = new QHBoxLayout();
    header->setSpacing(14);

    auto *iconLabel = new QLabel(this);
    iconLabel->setFixedSize(56, 56);
    iconLabel->setAlignment(Qt::AlignTop);

    QIcon useIcon = icon;
    if (useIcon.isNull()) {
        useIcon = windowIcon();
        if (useIcon.isNull()) {
            useIcon = this->windowIcon();
        }
    }

    if (QPixmap pm = useIcon.isNull() ? QPixmap() : useIcon.pixmap(56, 56); !pm.isNull()) {
        iconLabel->setPixmap(pm);
    }
    header->addWidget(iconLabel, 0);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(4);

    auto *title = new QLabel(m_info.app_name, this);
    title->setObjectName("AboutTitle");
    titleBox->addWidget(title);

    auto *subtitle = new QLabel(this);
    subtitle->setText(QString("<b>Version %1</b>  •  © %2 %3")
        .arg(m_info.version, m_info.year, m_info.author));
    subtitle->setTextFormat(Qt::RichText);
    subtitle->setObjectName("AboutSubtitle");
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleBox->addWidget(subtitle);

    auto *desc = new QLabel(m_info.description, this);
    desc->setWordWrap(true);
    desc->setObjectName("AboutDesc");
    titleBox->addWidget(desc);

    header->addLayout(titleBox, 1);
    root->addLayout(header);

    // --- Separator
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // --- Links + Details (rich)
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setObjectName("AboutBrowser");
    m_browser->setMinimumHeight(150);

    QStringList links;
    if (!m_info.website_url.isEmpty()) {
        links << QString("<a href=\"%1\">%2</a>")
                .arg(m_info.website_url, m_info.website_text);
    }
    if (!m_info.license_url.isEmpty()) {
        links << QString("<a href=\"%1\">%2</a>")
                .arg(m_info.license_url, m_info.license_text);
    }

    const QString linksLine = links.isEmpty() ? QString() : links.join(" • ");

    QString detailsHtml;
    if (!m_info.details.trimmed().isEmpty()) {
        const QString escaped = htmlEscape(m_info.details);
        detailsHtml = QString(R"(
            <div style="margin-top:10px;">
              <div style="font-weight:600; margin-bottom:6px;">Details</div>
              <pre style="
                margin:0;
                padding:10px 12px;
                border-radius:10px;
                background: rgba(0,0,0,0.03);
                border: 1px solid rgba(0,0,0,0.05);
                white-space: pre-wrap;
                font-family: ui-monospace, Consolas, Menlo, monospace;
              ">%1</pre>
            </div>
        )").arg(escaped);
    }

    const QString html = QString(R"(
        <div style="line-height:1.35;">
          <div>%1</div>
          %2
        </div>
    )").arg(linksLine, detailsHtml).trimmed();

    m_browser->setHtml(html);
    root->addWidget(m_browser);

    // --- Buttons (Copy Info + OK)
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    if (auto *okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setText("OK");
    }

    auto *copyBtn = new QPushButton("Copy Info", this);
    connect(copyBtn, &QPushButton::clicked, this, &AboutDialog::copyInfo);

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(copyBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(buttons);
    root->addLayout(btnRow);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    applyStyles();
}

void AboutDialog::copyInfo() {
    QString text = QString("%1 %2\n© %3 %4\n")
            .arg(m_info.app_name, m_info.version, m_info.year, m_info.author);

    if (!m_info.website_url.isEmpty()) {
        text += QString("%1: %2\n").arg(m_info.website_text, m_info.website_url);
    }
    if (!m_info.license_url.isEmpty()) {
        text += QString("%1: %2\n").arg(m_info.license_text, m_info.license_url);
    }
    if (!m_info.details.trimmed().isEmpty()) {
        text += "\n" + m_info.details.trimmed() + "\n";
    }

    QGuiApplication::clipboard()->setText(text);
}

void AboutDialog::applyStyles() {
    // Lightweight, modern-ish styling without fighting OS theme too much
    setStyleSheet(R"(
        QLabel#AboutTitle {
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#AboutSubtitle {
            color: rgba(0,0,0,0.62);
        }
        QLabel#AboutDesc {
            color: rgba(0,0,0,0.75);
        }
        QTextBrowser#AboutBrowser {
            color: rgba(0,0,0,0.85);
        }
    )");
}

QString AboutDialog::htmlEscape(const QString &s) {
    // Equivalent to your manual &,<,> escaping (also escapes quotes safely)
    QString out = s.toHtmlEscaped();

    // NOTE: Keep newlines; <pre style="white-space: pre-wrap;"> handles them.
    return out;
}
