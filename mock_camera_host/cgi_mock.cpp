// Phase 0 목업 카메라 호스트 — zoom/focus CGI mock.
// CctvControlService가 실제로 보내는 요청(POST /channel/{channelId}/zoom|focus, body {"value": N})에
// 맞춰 항상 OK 응답만 돌려준다. 설계 원칙 4: 정교한 카메라 재현이 아니라 VMS 쪽 흐름 검증용.
// 단순 목업이라 연결을 하나씩 blocking으로 처리한다 (VMS는 순차적으로 제어 요청을 보냄).

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

constexpr quint16 kPort = 8081;

void handleRequest(QTcpSocket *socket)
{
    QByteArray buf = socket->readAll();
    while (!buf.contains("\r\n\r\n")) {
        if (!socket->waitForReadyRead(500)) {
            socket->disconnectFromHost();
            return;
        }
        buf += socket->readAll();
    }

    const int headerEnd = buf.indexOf("\r\n\r\n");
    const QByteArray header = buf.left(headerEnd);
    const QList<QByteArray> headerLines = header.split('\n');
    if (headerLines.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    const QList<QByteArray> requestParts = headerLines.first().trimmed().split(' ');
    const QString method = requestParts.size() > 0 ? QString::fromLatin1(requestParts.at(0)) : QString();
    const QString path = requestParts.size() > 1 ? QString::fromLatin1(requestParts.at(1)) : QString();

    static const QRegularExpression clPattern(QStringLiteral("(?im)^content-length:\\s*(\\d+)"));
    int contentLength = 0;
    const auto clMatch = clPattern.match(QString::fromLatin1(header));
    if (clMatch.hasMatch()) {
        contentLength = clMatch.captured(1).toInt();
    }

    const int bodyStart = headerEnd + 4;
    while (buf.size() - bodyStart < contentLength) {
        if (!socket->waitForReadyRead(500)) {
            break;
        }
        buf += socket->readAll();
    }
    const QByteArray body = buf.mid(bodyStart, contentLength);

    static const QRegularExpression pathPattern(QStringLiteral("^/channel/(\\d+)/(zoom|focus)$"));
    const QRegularExpressionMatch match = pathPattern.match(path);

    int statusCode = 200;
    QString statusText = QStringLiteral("OK");
    QByteArray responseBody = R"({"data":{"result":"OK"}})";

    if (method != QStringLiteral("POST") || !match.hasMatch()) {
        statusCode = 404;
        statusText = QStringLiteral("Not Found");
        responseBody = R"({"error":"not_found"})";
        qWarning().noquote() << "cgi_mock: 처리 못한 요청" << method << path;
    } else {
        const int value = QJsonDocument::fromJson(body).object().value(QStringLiteral("value")).toInt();
        qInfo().noquote() << QString("cgi_mock: channel=%1 action=%2 value=%3 -> OK")
                                  .arg(match.captured(1), match.captured(2), QString::number(value));
    }

    const QByteArray response = QStringLiteral(
                                     "HTTP/1.1 %1 %2\r\n"
                                     "Content-Type: application/json\r\n"
                                     "Content-Length: %3\r\n"
                                     "Connection: close\r\n"
                                     "\r\n")
                                     .arg(statusCode)
                                     .arg(statusText)
                                     .arg(responseBody.size())
                                     .toUtf8()
        + responseBody;

    socket->write(response);
    socket->waitForBytesWritten(1000);
    socket->disconnectFromHost();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            if (socket->waitForReadyRead(1000)) {
                handleRequest(socket);
            }
            socket->deleteLater();
        }
    });

    if (!server.listen(QHostAddress::Any, kPort)) {
        qCritical().noquote() << "cgi_mock: 포트" << kPort << "리슨 실패" << server.errorString();
        return 1;
    }
    qInfo().noquote() << QString("cgi_mock: listening on 0.0.0.0:%1 (POST /channel/{channelId}/zoom|focus)").arg(kPort);

    return app.exec();
}
