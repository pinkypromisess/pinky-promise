import { useCallback, useEffect, useState } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import {
  ActivityIndicator,
  Button,
  FlatList,
  KeyboardAvoidingView,
  Platform,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import {
  confirmPinkyPromise,
  getConversation,
  initiatePinkyPromise,
  listMessages,
  postMessage,
} from '../api/conversations';
import type { Conversation, Message, PinkyPromise } from '../api/conversations';
import { getUserProfile } from '../api/otherProfile';
import type { ProfileBareView } from '../api/otherProfile';
import { getMyProfile } from '../api/profile';
import type { RootStackParamList } from '../navigation/types';
import { formatExpiryCountdown } from '../utils/countdown';
import { pinkyPromiseAffordance } from '../utils/pinkyPromiseAffordance';

type Props = NativeStackScreenProps<RootStackParamList, 'ConversationDetail'>;

// CUJ #4 conversation detail: message thread (oldest -> newest), a text
// composer, a visible expiry countdown, and the Pinky Promise
// initiate/confirm flow.
//
// KNOWN GAP (flagged to the module owner, not fixed here): there is no
// endpoint to fetch an existing PinkyPromise by id, or to ask "does this
// conversation already have a pending one" other than the response of the
// initiate call itself. `pinkyPromise` below is therefore local,
// session-only state -- it does not survive a re-focus/app-restart. In that
// case A's initiate button may show again even though one is already
// pending; the backend correctly rejects the retry with 409
// `PINKY_PROMISE_EXISTS`, which `handleInitiate` handles below rather than
// crashing. `conversation.status` (`active` / `pinky_promised` / `expired`)
// IS reliable and persists, and is used wherever possible.
export default function ConversationDetailScreen({ navigation, route }: Props) {
  const { conversationId } = route.params;

  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [myUserId, setMyUserId] = useState<string | null>(null);
  const [conversation, setConversation] = useState<Conversation | null>(null);
  const [otherProfile, setOtherProfile] = useState<ProfileBareView | null>(null);
  const [messages, setMessages] = useState<Message[]>([]);

  const [composerText, setComposerText] = useState('');
  const [sending, setSending] = useState(false);
  const [sendError, setSendError] = useState<string | null>(null);

  const [pinkyPromise, setPinkyPromise] = useState<PinkyPromise | null>(null);
  const [ppLoading, setPpLoading] = useState(false);
  const [ppError, setPpError] = useState<string | null>(null);

  // Forces a re-render every 30s so the countdown text stays live while the
  // screen is open, without refetching from the server.
  const [, setTick] = useState(0);
  useEffect(() => {
    const interval = setInterval(() => setTick((t) => t + 1), 30000);
    return () => clearInterval(interval);
  }, []);

  useFocusEffect(
    useCallback(() => {
      let cancelled = false;
      setLoading(true);
      setLoadError(null);

      (async () => {
        try {
          const [me, conv, messageList] = await Promise.all([
            getMyProfile(),
            getConversation(conversationId),
            listMessages(conversationId),
          ]);
          if (cancelled) return;

          const otherUserId = conv.proposer_user_id === me.user_id ? conv.interested_user_id : conv.proposer_user_id;
          const other = await getUserProfile(otherUserId);
          if (cancelled) return;

          setMyUserId(me.user_id);
          setConversation(conv);
          setMessages(messageList.messages);
          setOtherProfile(other);
          navigation.setOptions({ title: other.name });
        } catch (err) {
          if (!cancelled) {
            setLoadError(err instanceof ApiError ? err.body.message : 'Failed to load conversation.');
          }
        } finally {
          if (!cancelled) setLoading(false);
        }
      })();

      return () => {
        cancelled = true;
      };
    }, [conversationId, navigation]),
  );

  const handleSend = async () => {
    const trimmed = composerText.trim();
    if (!trimmed || sending) return;
    setSending(true);
    setSendError(null);
    try {
      const message = await postMessage(conversationId, 'text', trimmed);
      setMessages((current) => [...current, message]);
      setComposerText('');
    } catch (err) {
      if (err instanceof ApiError && err.body.error === 'CONVERSATION_EXPIRED') {
        setSendError('This conversation has expired.');
      } else if (err instanceof ApiError) {
        setSendError(err.body.message);
      } else {
        setSendError('Failed to send message.');
      }
    } finally {
      setSending(false);
    }
  };

  const handleInitiate = async () => {
    setPpLoading(true);
    setPpError(null);
    try {
      const pp = await initiatePinkyPromise(conversationId);
      setPinkyPromise(pp);
    } catch (err) {
      if (err instanceof ApiError && err.body.error === 'PINKY_PROMISE_EXISTS') {
        setPpError("There's already a pending Pinky Promise on this conversation.");
      } else if (err instanceof ApiError && err.body.error === 'CONVERSATION_EXPIRED') {
        setPpError('This conversation has expired.');
      } else if (err instanceof ApiError) {
        setPpError(err.body.message);
      } else {
        setPpError('Failed to start the Pinky Promise.');
      }
    } finally {
      setPpLoading(false);
    }
  };

  const handleConfirm = async () => {
    if (!pinkyPromise) return;
    setPpLoading(true);
    setPpError(null);
    try {
      const confirmed = await confirmPinkyPromise(pinkyPromise.id);
      setPinkyPromise(confirmed);
      setConversation((current) => (current ? { ...current, status: 'pinky_promised', expires_at: null } : current));
    } catch (err) {
      if (err instanceof ApiError && err.body.error === 'PINKY_PROMISE_CAP_REACHED') {
        setPpError('One of you already has 3 upcoming Pinky Promises -- this one can\'t be confirmed.');
      } else if (err instanceof ApiError && err.body.error === 'CONVERSATION_EXPIRED') {
        setPpError('This conversation has expired.');
      } else if (err instanceof ApiError) {
        setPpError(err.body.message);
      } else {
        setPpError('Failed to confirm the Pinky Promise.');
      }
    } finally {
      setPpLoading(false);
    }
  };

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  if (loadError || !conversation || !myUserId) {
    return (
      <View style={styles.center}>
        <Text testID="load-error" style={styles.error}>
          {loadError ?? 'Conversation not found.'}
        </Text>
      </View>
    );
  }

  const isA = conversation.proposer_user_id === myUserId;
  const affordance = pinkyPromiseAffordance({ conversationStatus: conversation.status, isA, pinkyPromise });

  return (
    <KeyboardAvoidingView
      style={styles.flex}
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
      keyboardVerticalOffset={80}
    >
      <View style={styles.header}>
        <Text testID="expiry-countdown" style={styles.countdown}>
          {formatExpiryCountdown(conversation.expires_at)}
        </Text>

        {affordance === 'initiate' && (
          <Button testID="pinky-promise-initiate" title="Pinky Promise" onPress={handleInitiate} disabled={ppLoading} />
        )}
        {affordance === 'waiting_on_b' && (
          <Text testID="pinky-promise-waiting" style={styles.ppPending}>
            Waiting for {otherProfile?.name ?? 'them'} to confirm...
          </Text>
        )}
        {affordance === 'confirm' && (
          <Button testID="pinky-promise-confirm" title="Confirm Pinky Promise" onPress={handleConfirm} disabled={ppLoading} />
        )}
        {affordance === 'promised' && (
          <Text testID="pinky-promise-badge" style={styles.ppPromised}>
            Pinky Promised!
          </Text>
        )}
        {ppError && (
          <Text testID="pinky-promise-error" style={styles.error}>
            {ppError}
          </Text>
        )}
      </View>

      <FlatList
        testID="message-list"
        style={styles.flex}
        data={messages}
        keyExtractor={(m) => m.id}
        contentContainerStyle={styles.messageListContent}
        ListEmptyComponent={<Text style={styles.empty}>No messages yet. Say hello!</Text>}
        renderItem={({ item }) => {
          const mine = item.sender_user_id === myUserId;
          return (
            <View testID={`message-${item.id}`} style={[styles.bubble, mine ? styles.bubbleMine : styles.bubbleTheirs]}>
              <Text style={mine ? styles.bubbleTextMine : styles.bubbleTextTheirs}>{item.content}</Text>
            </View>
          );
        }}
      />

      {sendError && (
        <Text testID="send-error" style={styles.error}>
          {sendError}
        </Text>
      )}

      <View style={styles.composer}>
        <TextInput
          testID="composer-input"
          style={styles.input}
          placeholder="Type a message"
          value={composerText}
          onChangeText={setComposerText}
          multiline
        />
        <Button title={sending ? 'Sending...' : 'Send'} onPress={handleSend} disabled={sending || !composerText.trim()} />
      </View>
    </KeyboardAvoidingView>
  );
}

const styles = StyleSheet.create({
  flex: { flex: 1 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24 },
  error: { color: '#b00020', marginTop: 8, paddingHorizontal: 16 },
  empty: { textAlign: 'center', color: '#666', marginTop: 32 },
  header: { padding: 16, borderBottomWidth: StyleSheet.hairlineWidth, borderBottomColor: '#ddd' },
  countdown: { fontWeight: '600', color: '#444', marginBottom: 8 },
  ppPending: { color: '#8a6d00' },
  ppPromised: { color: '#1e7e34', fontWeight: '700' },
  messageListContent: { padding: 16 },
  bubble: { borderRadius: 12, paddingVertical: 8, paddingHorizontal: 12, marginBottom: 8, maxWidth: '80%' },
  bubbleMine: { backgroundColor: '#2f6feb', alignSelf: 'flex-end' },
  bubbleTheirs: { backgroundColor: '#eee', alignSelf: 'flex-start' },
  bubbleTextMine: { color: '#fff' },
  bubbleTextTheirs: { color: '#222' },
  composer: { flexDirection: 'row', alignItems: 'flex-end', padding: 12, gap: 8, borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: '#ddd' },
  input: { flex: 1, borderWidth: 1, borderColor: '#ccc', borderRadius: 8, paddingHorizontal: 12, paddingVertical: 8, maxHeight: 100 },
});
