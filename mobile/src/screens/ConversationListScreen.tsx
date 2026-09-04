import { useCallback, useState } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import { ActivityIndicator, FlatList, Image, StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import { listConversations } from '../api/conversations';
import type { Conversation } from '../api/conversations';
import { getUserProfile } from '../api/otherProfile';
import { getMyProfile } from '../api/profile';
import type { RootStackParamList } from '../navigation/types';
import { formatExpiryCountdown } from '../utils/countdown';

type Props = NativeStackScreenProps<RootStackParamList, 'Conversations'>;

interface ConversationRow {
  conversation: Conversation;
  otherName: string;
  otherPhotoUrl: string | null;
}

// CUJ #4: list every Conversation the caller is a participant in, most-recent
// first (server order), identifying the other participant by name + photo
// (via the bare-view GET /profile/{user_id}) and showing expiry/status.
export default function ConversationListScreen({ navigation }: Props) {
  const [rows, setRows] = useState<ConversationRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  useFocusEffect(
    useCallback(() => {
      let cancelled = false;
      setLoading(true);
      setErrorMessage(null);

      (async () => {
        try {
          const me = await getMyProfile();
          const { conversations } = await listConversations();
          const built = await Promise.all(
            conversations.map(async (conversation) => {
              const otherUserId =
                conversation.proposer_user_id === me.user_id
                  ? conversation.interested_user_id
                  : conversation.proposer_user_id;
              const other = await getUserProfile(otherUserId);
              const sortedPhotos = other.photos.slice().sort((a, b) => a.position - b.position);
              return {
                conversation,
                otherName: other.name,
                otherPhotoUrl: sortedPhotos[0]?.url ?? null,
              };
            }),
          );
          if (!cancelled) setRows(built);
        } catch (err) {
          if (!cancelled) {
            setErrorMessage(err instanceof ApiError ? err.body.message : 'Failed to load conversations.');
          }
        } finally {
          if (!cancelled) setLoading(false);
        }
      })();

      return () => {
        cancelled = true;
      };
    }, []),
  );

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  return (
    <View style={styles.container}>
      {errorMessage && (
        <Text testID="conversations-error" style={styles.error}>
          {errorMessage}
        </Text>
      )}
      <FlatList
        testID="conversation-list"
        data={rows}
        keyExtractor={(row) => row.conversation.id}
        ListEmptyComponent={<Text style={styles.empty}>No conversations yet.</Text>}
        renderItem={({ item }) => (
          <TouchableOpacity
            testID={`conversation-row-${item.conversation.id}`}
            accessibilityRole="button"
            style={styles.row}
            onPress={() => navigation.navigate('ConversationDetail', { conversationId: item.conversation.id })}
          >
            {item.otherPhotoUrl && <Image source={{ uri: item.otherPhotoUrl }} style={styles.avatar} />}
            <View style={styles.rowText}>
              <Text testID={`conversation-name-${item.conversation.id}`} style={styles.name}>
                {item.otherName}
              </Text>
              <Text testID={`conversation-status-${item.conversation.id}`} style={styles.status}>
                {formatExpiryCountdown(item.conversation.expires_at)}
              </Text>
            </View>
          </TouchableOpacity>
        )}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  error: { color: '#b00020', padding: 16 },
  empty: { textAlign: 'center', color: '#666', marginTop: 32 },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: 16,
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: '#ddd',
  },
  avatar: { width: 56, height: 56, borderRadius: 28, marginRight: 12, backgroundColor: '#eee' },
  rowText: { flex: 1 },
  name: { fontSize: 16, fontWeight: '600' },
  status: { color: '#666', marginTop: 4 },
});
