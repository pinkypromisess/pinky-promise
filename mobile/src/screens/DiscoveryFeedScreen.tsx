import { useCallback, useState } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import { ActivityIndicator, Alert, ScrollView, StyleSheet, Text, TouchableOpacity, View } from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import { getMyProfile } from '../api/profile';
import { getFeed, swipeOnProposal } from '../api/proposals';
import type { ProposalFeedItem } from '../api/proposals';
import ProposalCard from '../components/ProposalCard';
import type { RootStackParamList } from '../navigation/types';

type Props = NativeStackScreenProps<RootStackParamList, 'Discovery'>;

// CUJ #3: browse Proposals one at a time. Any user can browse and pass (X);
// Hearting requires verification (CUJ #2). Advancing to the next card only
// happens via X or Heart -- there is deliberately no swipe-to-advance
// gesture, so the ScrollView here only scrolls *within* the current card.
export default function DiscoveryFeedScreen({ navigation }: Props) {
  const [items, setItems] = useState<ProposalFeedItem[] | null>(null);
  const [index, setIndex] = useState(0);
  const [verified, setVerified] = useState(false);
  const [loading, setLoading] = useState(true);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [swiping, setSwiping] = useState(false);

  useFocusEffect(
    useCallback(() => {
      let cancelled = false;
      setLoading(true);
      setErrorMessage(null);

      Promise.all([getFeed(), getMyProfile()])
        .then(([feed, profile]) => {
          if (cancelled) return;
          setItems(feed.proposals);
          setIndex(0);
          setVerified(profile.verified);
        })
        .catch((err) => {
          if (cancelled) return;
          setErrorMessage(err instanceof Error ? err.message : 'Failed to load the feed.');
        })
        .finally(() => {
          if (!cancelled) setLoading(false);
        });

      return () => {
        cancelled = true;
      };
    }, []),
  );

  const current = items && index < items.length ? items[index] : null;

  const advance = () => {
    setIndex((i) => i + 1);
  };

  const handlePass = async () => {
    if (!current || swiping) return;
    setSwiping(true);
    try {
      await swipeOnProposal(current.proposal.id, 'pass');
      advance();
    } catch (err) {
      handleSwipeError(err);
    } finally {
      setSwiping(false);
    }
  };

  const handleHeart = async () => {
    if (!current || swiping) return;

    if (!verified) {
      navigation.navigate('Verification');
      return;
    }

    setSwiping(true);
    try {
      await swipeOnProposal(current.proposal.id, 'interested');
      advance();
    } catch (err) {
      handleSwipeError(err);
    } finally {
      setSwiping(false);
    }
  };

  const handleSwipeError = (err: unknown) => {
    if (err instanceof ApiError) {
      if (err.body.error === 'PROFILE_NOT_VERIFIED') {
        // Defensive fallback: local `verified` flag was stale. Send the user
        // to verify instead of advancing (no interest was actually recorded).
        navigation.navigate('Verification');
        return;
      }
      if (err.body.error === 'INTERESTED_DAILY_CAP_REACHED') {
        Alert.alert("That's your limit for today", "You've hit the 10-interested-per-day limit. Try again tomorrow.");
        return;
      }
      if (err.body.error === 'ALREADY_SWIPED' || err.body.error === 'PROPOSAL_NOT_FOUND') {
        // Stale card (already actioned elsewhere, or gone) -- just move on.
        advance();
        return;
      }
      Alert.alert('Something went wrong', err.body.message);
      return;
    }
    Alert.alert('Something went wrong', err instanceof Error ? err.message : 'Please try again.');
  };

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  if (errorMessage) {
    return (
      <View style={styles.center}>
        <Text style={styles.error}>{errorMessage}</Text>
      </View>
    );
  }

  if (!current) {
    return (
      <View testID="empty-state" style={styles.center}>
        <Text style={styles.emptyTitle}>Nothing new right now</Text>
        <Text style={styles.emptyBody}>Check back later for more Proposals.</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <ProposalCard item={current} />
      </ScrollView>
      <View style={styles.actionBar}>
        <TouchableOpacity
          accessibilityRole="button"
          accessibilityLabel="Pass"
          style={[styles.actionButton, styles.passButton]}
          onPress={handlePass}
          disabled={swiping}
        >
          <Text style={styles.actionIcon}>✕</Text>
        </TouchableOpacity>
        <TouchableOpacity
          accessibilityRole="button"
          accessibilityLabel="Heart"
          style={[styles.actionButton, styles.heartButton]}
          onPress={handleHeart}
          disabled={swiping}
        >
          <Text style={styles.actionIcon}>♥</Text>
        </TouchableOpacity>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24 },
  error: { color: '#b00020', textAlign: 'center' },
  emptyTitle: { fontSize: 20, fontWeight: '700', marginBottom: 8 },
  emptyBody: { color: '#666', textAlign: 'center' },
  scrollContent: { paddingBottom: 112 },
  actionBar: {
    position: 'absolute',
    left: 0,
    right: 0,
    bottom: 0,
    flexDirection: 'row',
    justifyContent: 'center',
    gap: 32,
    paddingVertical: 16,
    backgroundColor: '#fff',
    borderTopWidth: StyleSheet.hairlineWidth,
    borderTopColor: '#ddd',
  },
  actionButton: {
    width: 64,
    height: 64,
    borderRadius: 32,
    alignItems: 'center',
    justifyContent: 'center',
  },
  passButton: { backgroundColor: '#fdecea' },
  heartButton: { backgroundColor: '#fde6ec' },
  actionIcon: { fontSize: 28 },
});
