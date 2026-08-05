"""!
@file alert_service.py
@brief Telegram alert notification and command handler module.
@details Registers bot command handlers (/start, /status) and dispatches formatted
         accident notifications with cooldown protection.
"""

import time
from typing import Set

from arduino.app_bricks.telegram_bot import TelegramBot, Sender, Message
from arduino.app_utils import Logger, Bridge
import config
import telemetry

logger = Logger("alert-service")

## @brief Set of registered Telegram chat IDs.
known_chat_ids: Set[int] = set()

## @brief Last accident Telegram alert dispatch timestamp.
_last_accident_ts: float = 0.0


def register_chat(sender: Sender) -> None:
    """!
    @brief Registers a Telegram chat ID for emergency alerts.
    @param sender Sender object representing Telegram user chat.
    @return None
    """
    if hasattr(sender, "chat_id") and sender.chat_id:
        if sender.chat_id not in known_chat_ids:
            known_chat_ids.add(sender.chat_id)
            logger.info(f"Registered Telegram chat_id: {sender.chat_id}")


def start_cmd(sender: Sender, message: Message) -> None:
    """!
    @brief Handler for Telegram /start command.
    @param sender Sender object.
    @param message Message object.
    @return None
    """
    register_chat(sender)
    sender.reply(
        "*INCIDENT DETECTION SYSTEM*\n"
        "Status: Active\n\n"
        "You are registered for critical safety notifications."
    )


def status_cmd(sender: Sender, message: Message) -> None:
    """!
    @brief Handler for Telegram /status command.
    @param sender Sender object.
    @param message Message object.
    @return None
    """
    register_chat(sender)
    with telemetry._state_lock:
        cls = telemetry.state["last_classification"]
    sender.reply(
        "*SYSTEM TELEMETRY REPORT*\n"
        "-----------------------\n"
        f"State: `{cls}`"
    )


def on_text_msg(sender: Sender, message: Message) -> None:
    """!
    @brief Fallback text handler for incoming Telegram messages.
    @param sender Sender object.
    @param message Message object.
    @return None
    """
    register_chat(sender)
    sender.reply(f"Chat ID {sender.chat_id} registered for safety alerts.")


def setup_telegram_bot(bot: TelegramBot) -> None:
    """!
    @brief Binds commands and text callbacks to TelegramBot instance.
    @param bot TelegramBot brick instance.
    @return None
    """
    bot.add_command("start", start_cmd, "Register for safety alerts")
    bot.add_command("status", status_cmd, "View system telemetry")
    bot.on_text(on_text_msg)


def dispatch_accident_alert(bot: TelegramBot, cls: dict) -> None:
    """!
    @brief Dispatches emergency accident notification to all registered Telegram chats.
    @param bot TelegramBot instance.
    @param cls Classification confidence dictionary.
    @return None
    """
    global _last_accident_ts
    now = time.time()
    if now - _last_accident_ts < config.ACCIDENT_COOLDOWN_S:
        logger.info("Accident alert skipped due to active cooldown period")
        return

    _last_accident_ts = now
    confidence_pct = round(cls.get("Accident", 0) * 100, 1)

    msg = (
        "*CRITICAL ALERT: ACCIDENT DETECTED*\n"
        "-----------------------------------\n"
        f"Confidence Level: `{confidence_pct}%`\n"
        f"Timestamp: `{time.strftime('%Y-%m-%d %H:%M:%S')}`\n\n"
        "System: Arduino UNO Q Incident Monitor"
    )


    alert_sent = False

    if known_chat_ids:
        for cid in list(known_chat_ids):
            for method_name in ["send_message", "send_text", "send"]:
                if hasattr(bot, method_name):
                    try:
                        getattr(bot, method_name)(cid, msg)
                        alert_sent = True
                        logger.info(f"Accident alert sent to Telegram chat {cid} via {method_name}")
                        break
                    except Exception as e:
                        logger.warning(f"bot.{method_name}({cid}) failed: {e}")

    if not alert_sent and hasattr(bot, "broadcast"):
        try:
            bot.broadcast(msg)
            alert_sent = True
            logger.info("Accident alert broadcasted to Telegram users")
        except Exception as e:
            logger.warning(f"bot.broadcast failed: {e}")

    try:
        Bridge.notify("show_alert")
    except Exception as e:
        logger.warning(f"Bridge notification show_alert failed: {e}")
