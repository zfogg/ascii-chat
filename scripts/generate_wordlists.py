#!/usr/bin/env python3
"""
Generate massive wordlists for ACDS session strings.

This script extracts high-quality adjectives and nouns from public domain
word frequency datasets, filters them for UX appropriateness, and outputs
C-compatible arrays.

Target: 2000+ words per category = 8+ billion combinations

Data sources:
1. SCOWL (Spell Checker Oriented Word Lists) - primary
2. Google Ngrams frequency data - for ranking
3. NLTK Brown corpus - for POS tagging

Requirements:
    pip install nltk
    python -c "import nltk; nltk.download('brown'); nltk.download('averaged_perceptron_tagger')"
"""

import re
import sys
import urllib.request
import gzip
import json
from collections import defaultdict
from pathlib import Path

try:
    import nltk
    from nltk.corpus import brown, wordnet
    from nltk import pos_tag
except ImportError:
    print("ERROR: NLTK not installed. Run: pip install nltk", file=sys.stderr)
    sys.exit(1)


class WordlistGenerator:
    """Generate filtered word lists for session strings."""

    # Words to exclude (profanity, negative sentiment, confusing, common names)
    BLACKLIST = {
        # Profanity and inappropriate
        "ass", "damn", "hell", "crap", "piss", "cock", "dick", "fuck", "shit",
        "bitch", "bastard", "whore", "slut", "fag", "dyke", "retard",
        # Negative/violent
        "kill", "death", "dead", "dying", "murder", "rape", "abuse", "torture",
        "pain", "suffer", "agony", "misery", "doom", "dread", "terror", "horror",
        "evil", "wicked", "cruel", "brutal", "savage", "vicious", "nasty",
        "ugly", "disgusting", "gross", "filthy", "rotten", "putrid", "foul",
        # Confusing/ambiguous
        "anal", "oral", "genital", "penis", "vagina", "breast", "nipple",
        # Medical/sensitive
        "cancer", "tumor", "disease", "virus", "infection", "plague",
        # Political/controversial
        "nazi", "fascist", "communist", "terrorist", "jihad",
        # Common first names (top 200 US names to exclude)
        "aaron", "abby", "abdul", "abe", "abigail", "abraham", "adam", "addison", "adele",
        "adrian", "adriana", "ahmed", "aiden", "alana", "albert", "alberto", "alejandro",
        "alex", "alexa", "alexander", "alexandra", "alexandria", "alexis", "alfred", "alice",
        "alicia", "alison", "allen", "allison", "alma", "alyssa", "amanda", "amber", "amelia",
        "ames", "ami", "amy", "ana", "andre", "andrea", "andres", "andrew", "andy", "angel",
        "angela", "angelica", "angelo", "angie", "anita", "ann", "anna", "anne", "annette",
        "annie", "anthony", "antoine", "anton", "antonio", "antony", "anya", "april", "archie",
        "ari", "ariana", "ariel", "arthur", "asher", "ashley", "ashton", "astrid", "attila",
        "aubrey", "audrey", "aurora", "austin", "autumn", "ava", "avery", "axel",
        "bailey", "barbara", "barry", "beatrice", "bella", "ben", "benjamin", "bennett",
        "benny", "beth", "betty", "beverly", "bianca", "bill", "billy", "blake", "blanca",
        "bob", "bobby", "bonnie", "boyd", "bradley", "brady", "brandon", "brenda", "brendan",
        "brent", "brett", "brian", "brianna", "bridget", "brittany", "brook", "brooke",
        "brooklyn", "bruce", "bruno", "bryan", "bryant", "bryce", "caitlin", "caleb",
        "calvin", "cameron", "camila", "candace", "cara", "carey", "carl", "carla", "carlos",
        "carmen", "carol", "caroline", "carolyn", "carrie", "carson", "carter", "casey",
        "cassandra", "catherine", "cathy", "cecilia", "cedric", "cesar", "chad", "chandler",
        "charles", "charlie", "charlotte", "chase", "chelsea", "cheryl", "chester", "chloe",
        "chris", "christian", "christina", "christine", "christopher", "cindy", "claire",
        "clara", "clarence", "clark", "claudia", "clay", "clayton", "clifford", "clinton",
        "clyde", "cody", "cole", "colin", "colleen", "connie", "connor", "constance",
        "cooper", "cora", "corey", "courtney", "craig", "cristian", "cruz", "crystal",
        "curtis", "cynthia", "dale", "dallas", "dalton", "damian", "dana", "dane", "daniel",
        "danielle", "danny", "dante", "daphne", "darius", "darrell", "darren", "darryl",
        "darwin", "dave", "david", "davis", "dawn", "dean", "deanna", "debbie", "deborah",
        "debra", "dee", "delia", "dennis", "derek", "derrick", "desmond", "destiny",
        "devin", "diana", "diane", "dick", "diego", "dillon", "dolores", "dominic",
        "don", "donald", "donna", "donovan", "dora", "doris", "dorothy", "doug", "douglas",
        "drake", "drew", "duane", "duncan", "dustin", "dylan", "earl", "eddie", "edgar",
        "edith", "edmond", "edmund", "eduardo", "edward", "edwin", "eileen", "elaine",
        "eleanor", "elena", "eli", "elias", "elijah", "elise", "eliza", "elizabeth",
        "ella", "ellen", "elliott", "ellis", "elmer", "eloise", "elsa", "elsie", "elvis",
        "emerson", "emily", "emma", "emmanuel", "emmett", "enrique", "eric", "erica",
        "erik", "erin", "ernest", "esther", "ethan", "eugene", "eva", "evan", "evelyn",
        "everett", "ezra", "faith", "felix", "fernando", "flora", "florence", "floyd",
        "forrest", "frances", "francis", "francisco", "frank", "franklin", "fred",
        "frederick", "gabriel", "gail", "garret", "garrett", "gary", "gavin", "gene",
        "genesis", "geoffrey", "george", "georgia", "gerald", "gerard", "gilbert", "gina",
        "giovanni", "giselle", "glen", "glenn", "gloria", "gordon", "grace", "graham",
        "grant", "greg", "gregory", "griffin", "guy", "hank", "hannah", "harold", "harper",
        "harriet", "harris", "harrison", "harry", "harvey", "hazel", "heath", "heather",
        "hector", "heidi", "helen", "helena", "henry", "herbert", "herman", "holly",
        "homer", "howard", "hudson", "hugh", "hugo", "hunter", "ian", "ibrahim", "ida",
        "igor", "irene", "iris", "irma", "irving", "isaac", "isabel", "isabella", "isaiah",
        "ivan", "jack", "jackie", "jackson", "jacob", "jacqueline", "jade", "jaime",
        "jake", "james", "jamie", "jane", "janet", "janice", "jared", "jasmine", "jason",
        "jasper", "javier", "jay", "jean", "jeanette", "jeanne", "jeff", "jefferson",
        "jeffrey", "jennifer", "jenny", "jeremiah", "jeremy", "jerome", "jerry", "jess",
        "jesse", "jessica", "jesus", "jill", "jim", "jimmy", "joan", "joanna", "joanne",
        "joaquin", "jocelyn", "joe", "joel", "joey", "john", "johnathan", "johnny",
        "johnson", "jon", "jonah", "jonathan", "jones", "jordan", "jorge", "jose", "josefa",
        "joseph", "josephine", "josh", "joshua", "josiah", "joy", "joyce", "juan", "juanita",
        "judith", "judy", "julian", "julie", "julius", "june", "justin", "kaitlyn", "karen",
        "kari", "karl", "kate", "katherine", "kathleen", "kathryn", "kathy", "katie",
        "katrina", "kay", "kayla", "kaylee", "keith", "kelly", "kelsey", "kendall",
        "kennedy", "kenneth", "kenny", "kent", "kevin", "kim", "kimberly", "kirk",
        "kristen", "kristin", "kurt", "kyle", "lacey", "lance", "landon", "larry", "laura",
        "lauren", "laurence", "laurie", "lawrence", "leah", "lee", "leigh", "leo", "leon",
        "leona", "leonard", "leslie", "lester", "lewis", "liam", "lila", "lillian",
        "lily", "lincoln", "linda", "lindsay", "lindsey", "lionel", "lisa", "lloyd",
        "logan", "lois", "lopez", "lorenzo", "loretta", "lori", "lorraine", "louis",
        "louise", "lucas", "lucia", "lucille", "lucy", "luis", "luke", "luther", "lydia",
        "lynn", "mabel", "mack", "mackenzie", "maddie", "madeline", "madison", "mae",
        "magdalena", "maggie", "malcolm", "malik", "mallory", "manuel", "marc", "marcel",
        "marcia", "marco", "marcos", "marcus", "margaret", "maria", "marian", "marie",
        "marilyn", "marina", "mario", "marion", "marjorie", "mark", "marlene", "marlon",
        "marsha", "marshall", "martha", "martin", "martinez", "marty", "marvin", "mary",
        "mason", "mathew", "matilda", "matt", "matthew", "maureen", "maurice", "max",
        "maxine", "maxwell", "maya", "megan", "melanie", "melinda", "melissa", "melody",
        "melvin", "mercedes", "meredith", "micah", "michael", "michelle", "mickey",
        "miguel", "mike", "mildred", "miles", "milton", "mina", "mindy", "miranda",
        "miriam", "misty", "mitchell", "molly", "monica", "monique", "monroe", "montana",
        "morgan", "morris", "moses", "murphy", "murray", "myles", "myra", "myrtle",
        "nadia", "nancy", "naomi", "natalie", "natasha", "nathan", "nathaniel", "neal",
        "ned", "neil", "nellie", "nelson", "neville", "nia", "nicholas", "nick", "nicolas",
        "nicole", "nina", "noah", "noel", "nora", "norma", "norman", "octavia", "olive",
        "oliver", "olivia", "omar", "opal", "ophelia", "orlando", "oscar", "otis", "otto",
        "owen", "paige", "pam", "pamela", "parker", "pat", "patrice", "patricia", "patrick",
        "patsy", "patti", "patty", "paul", "paula", "pauline", "pearl", "pedro", "peggy",
        "penelope", "penny", "percy", "perry", "pete", "peter", "peterson", "peyton",
        "phil", "philip", "phillip", "phoebe", "phyllis", "pierce", "pierre", "preston",
        "quinn", "rachel", "rafael", "ralph", "ramona", "ramon", "randall", "randolph",
        "randy", "raul", "ray", "raymond", "rebecca", "reed", "reese", "regina", "reid",
        "renee", "reuben", "rex", "reynaldo", "rhonda", "ricardo", "richard", "rick",
        "ricky", "riley", "rita", "rob", "robert", "roberta", "roberto", "robin",
        "rochelle", "rocky", "rodney", "rodriguez", "roger", "roland", "roman", "romeo",
        "ron", "ronald", "rosa", "rosalie", "rose", "rosemary", "ross", "roxanne", "roy",
        "ruby", "rudolph", "rudy", "rufus", "russell", "ruth", "ryan", "sabrina", "sadie",
        "sally", "salvador", "sam", "samantha", "sammy", "samuel", "sandra", "sandy",
        "santiago", "santos", "sara", "sarah", "saul", "savannah", "sawyer", "scarlett",
        "scott", "sean", "sebastian", "selena", "sergio", "seth", "seymour", "shane",
        "shannon", "sharon", "shaun", "shawn", "sheila", "shelby", "sheldon", "shelly",
        "sherman", "sherri", "sherry", "sheryl", "shirley", "sidney", "sierra", "silas",
        "simon", "sofia", "solomon", "sonia", "sonya", "sophia", "sophie", "spencer",
        "stacy", "stan", "stanley", "stella", "stephanie", "stephen", "sterling", "steve",
        "steven", "stewart", "stuart", "sue", "sullivan", "summer", "susan", "susana",
        "susanne", "suzanne", "sylvester", "sylvia", "tabitha", "tamara", "tammy",
        "tanner", "tanya", "tara", "taryn", "tasha", "tatiana", "taylor", "ted", "teresa",
        "terrance", "terrence", "terri", "terry", "tessa", "theodore", "theresa", "thomas",
        "tiffany", "tim", "timmy", "timothy", "tina", "todd", "tom", "tommy", "toni",
        "tony", "tonya", "tracey", "traci", "tracy", "travis", "trevor", "trey", "tricia",
        "tristan", "troy", "tucker", "tyler", "tyrone", "ulysses", "valerie", "van",
        "vanessa", "vaughn", "vera", "veronica", "vicki", "vickie", "vicky", "victor",
        "victoria", "vincent", "viola", "violet", "virginia", "vivian", "wade", "wallace",
        "walter", "wanda", "ward", "warren", "wayne", "wendy", "wesley", "whitney",
        "wilbur", "wilfred", "will", "william", "willie", "willis", "wilma", "winston",
        "wolfgang", "wyatt", "xavier", "yolanda", "zachary", "zane", "zoe",
        # Common place names
        "afghanistan", "africa", "alabama", "alaska", "albania", "algeria", "america",
        "anderson", "argentina", "arizona", "arkansas", "armenia", "asia", "atlanta",
        "atlantic", "austin", "australia", "austria", "baghdad", "baltimore", "bangkok",
        "bangladesh", "beijing", "beirut", "belgium", "berlin", "boston", "brazil",
        "britain", "brooklyn", "budapest", "bulgaria", "cairo", "california", "cambodia",
        "canada", "carolina", "chicago", "chile", "china", "cleveland", "colombia",
        "colorado", "congo", "connecticut", "croatia", "cuba", "cyprus", "dallas",
        "delaware", "denmark", "detroit", "dublin", "ecuador", "egypt", "england",
        "ethiopia", "europe", "finland", "florida", "france", "frankfurt", "georgia",
        "germany", "glasgow", "greece", "greenland", "guatemala", "haiti", "hamburg",
        "hawaii", "holland", "honduras", "houston", "hungary", "iceland", "idaho",
        "illinois", "india", "indiana", "indonesia", "iowa", "iran", "iraq", "ireland",
        "israel", "istanbul", "italy", "jamaica", "japan", "jersey", "jerusalem",
        "jordan", "kansas", "kentucky", "kenya", "korea", "lebanon", "libya", "lisbon",
        "london", "louisiana", "madrid", "maine", "manchester", "manhattan", "maryland",
        "massachusetts", "melbourne", "memphis", "mexico", "miami", "michigan", "milan",
        "milwaukee", "minneapolis", "minnesota", "mississippi", "missouri", "montana",
        "montreal", "morocco", "moscow", "munich", "nebraska", "nepal", "netherlands",
        "nevada", "nigeria", "norway", "oakland", "ohio", "oklahoma", "oregon", "orlando",
        "oslo", "oxford", "pacific", "pakistan", "panama", "paris", "pennsylvania",
        "peru", "philadelphia", "philippines", "phoenix", "pittsburgh", "poland",
        "portland", "portugal", "prague", "quebec", "romania", "rome", "russia",
        "sacramento", "salvador", "santiago", "scotland", "seattle", "serbia", "shanghai",
        "singapore", "somalia", "spain", "stockholm", "sudan", "sweden", "switzerland",
        "sydney", "syria", "taiwan", "tennessee", "texas", "thailand", "tokyo", "toronto",
        "tunisia", "turkey", "uganda", "ukraine", "utah", "venezuela", "vermont", "vietnam",
        "virginia", "wales", "warsaw", "washington", "wisconsin", "wyoming", "yemen",
        "zimbabwe",
    }

    # Negative sentiment prefixes/suffixes
    NEGATIVE_PATTERNS = [
        r"^un", r"^dis", r"^non", r"^anti", r"^de",  # Prefixes
        r"less$", r"^mis", r"^mal", r"^pseudo",
    ]

    def __init__(self, min_length=3, max_length=12):
        self.min_length = min_length
        self.max_length = max_length
        self.adjectives = set()
        self.nouns = set()

        # Download NLTK data if needed
        print("Ensuring NLTK data is available...", file=sys.stderr)
        try:
            brown.words()
        except LookupError:
            nltk.download('brown', quiet=True)

        try:
            pos_tag(['test'], tagset='universal')
        except LookupError:
            nltk.download('averaged_perceptron_tagger', quiet=True)
            nltk.download('averaged_perceptron_tagger_eng', quiet=True)
            nltk.download('universal_tagset', quiet=True)

        try:
            wordnet.synsets('test')
        except LookupError:
            nltk.download('wordnet', quiet=True)
            nltk.download('omw-1.4', quiet=True)

    def is_valid_word(self, word: str, is_proper_noun: bool = False) -> bool:
        """Check if word meets quality criteria."""
        # Skip proper nouns - just check if first letter is uppercase
        if word and word[0].isupper():
            return False

        word_lower = word.lower()

        # Length check
        if len(word) < self.min_length or len(word) > self.max_length:
            return False

        # Only lowercase letters (no hyphens, apostrophes, etc.)
        if not re.match(r'^[a-z]+$', word_lower):
            return False

        # Skip words with repeated letters at start (interjections like aaa, aaah, aaaargh)
        if re.match(r'^(.)\1{2,}', word_lower):
            return False

        # Skip words with too many repeated letters anywhere (onomatopoeia)
        if re.search(r'(.)\1{3,}', word_lower):
            return False

        # Blacklist check
        if word_lower in self.BLACKLIST:
            return False

        # Negative sentiment patterns
        for pattern in self.NEGATIVE_PATTERNS:
            if re.search(pattern, word_lower):
                return False

        # Avoid words ending in 'ing' (gerunds - can be confusing)
        if word_lower.endswith('ing'):
            return False

        # Avoid words ending in 'ed' (past tense - prefer base forms)
        if word_lower.endswith('ed') and len(word) > 4:
            return False

        # Avoid words ending in 'ment', 'tion', 'sion' (abstract nouns - less memorable)
        if word_lower.endswith(('ment', 'tion', 'sion')) and len(word) > 6:
            return False

        # Avoid plurals for nouns (we want singular forms)
        # This is a heuristic - not perfect but good enough
        if word_lower.endswith('s') and len(word) > 4:
            # Check if it's likely a plural (ends in s but not ss)
            if not word_lower.endswith('ss') and not word_lower.endswith('us') and not word_lower.endswith('ous'):
                return False

        # Skip very short words with odd patterns
        if len(word_lower) == 3 and word_lower[0] == word_lower[2]:
            return False  # Avoid patterns like 'aaa', 'aba', etc.

        return True

    def extract_from_wordnet(self):
        """
        Extract adjectives and nouns from WordNet.

        WordNet is ideal because:
        - Only contains dictionary words (no proper nouns)
        - Semantically categorized
        - High quality, curated data
        """
        print("Extracting words from WordNet...", file=sys.stderr)

        adj_count = 0
        noun_count = 0

        # WordNet POS categories: 'a' = adjective, 'n' = noun
        for synset in wordnet.all_synsets('a'):  # All adjective synsets
            for lemma in synset.lemmas():
                word = lemma.name().lower().replace('_', ' ')  # Convert underscores to spaces
                # Skip multi-word phrases
                if ' ' in word:
                    continue
                if self.is_valid_word(word) and word not in self.adjectives:
                    self.adjectives.add(word)
                    adj_count += 1

        for synset in wordnet.all_synsets('n'):  # All noun synsets
            for lemma in synset.lemmas():
                word = lemma.name().lower().replace('_', ' ')
                # Skip multi-word phrases
                if ' ' in word:
                    continue
                if self.is_valid_word(word) and word not in self.nouns:
                    self.nouns.add(word)
                    noun_count += 1

        print(f"  Found {adj_count} adjectives, {noun_count} nouns from WordNet", file=sys.stderr)

    def extract_from_nltk_brown(self):
        """Extract adjectives and nouns from NLTK Brown corpus."""
        print("Extracting words from NLTK Brown corpus...", file=sys.stderr)

        # Get tagged words from Brown corpus with detailed POS tags
        tagged_words = brown.tagged_words()

        adj_count = 0
        noun_count = 0

        for word, tag in tagged_words:
            # Skip proper nouns by POS tag
            if tag in ('NNP', 'NNPS'):
                continue

            # is_valid_word will also check for capitalization
            if not self.is_valid_word(word):
                continue

            word_lower = word.lower()

            # JJ, JJR, JJS = adjectives (positive, comparative, superlative)
            if tag in ('JJ', 'JJR', 'JJS'):
                if word_lower not in self.adjectives:
                    self.adjectives.add(word_lower)
                    adj_count += 1

            # NN, NNS = common nouns (singular, plural)
            elif tag in ('NN', 'NNS'):
                if word_lower not in self.nouns:
                    self.nouns.add(word_lower)
                    noun_count += 1

        print(f"  Found {adj_count} new adjectives, {noun_count} new nouns from Brown corpus", file=sys.stderr)

    def download_scowl_wordlist(self, size=70):
        """
        Download SCOWL wordlist (American English).

        Size parameter:
        - 35: common words (~40k)
        - 50: medium vocabulary (~60k)
        - 70: large vocabulary (~115k) - RECOMMENDED
        - 80: huge vocabulary (~240k) - includes rare words
        """
        print(f"Downloading SCOWL wordlist (size={size})...", file=sys.stderr)

        # SCOWL download URL
        url = f"https://sourceforge.net/projects/wordlist/files/SCOWL/{size}/scowl-{size}.tar.gz/download"

        try:
            # Download and decompress
            print(f"  Fetching {url}", file=sys.stderr)
            response = urllib.request.urlopen(url, timeout=30)

            # For now, let's use a simpler approach - just download a plain text wordlist
            # The SCOWL tarball extraction is complex. Instead, use a simpler source.
            raise NotImplementedError("SCOWL download needs tarball extraction")

        except Exception as e:
            print(f"  Warning: SCOWL download failed: {e}", file=sys.stderr)
            print(f"  Continuing with NLTK data only...", file=sys.stderr)

    def download_google_10000(self):
        """Download Google 10000 most common English words."""
        print("Downloading Google 10000 common words...", file=sys.stderr)

        # This is a well-known dataset hosted on GitHub
        url = "https://raw.githubusercontent.com/first20hours/google-10000-english/master/google-10000-english-no-swears.txt"

        try:
            response = urllib.request.urlopen(url, timeout=30)
            words = response.read().decode('utf-8').splitlines()

            print(f"  Downloaded {len(words)} words", file=sys.stderr)

            # POS tag them to categorize
            print(f"  POS tagging...", file=sys.stderr)
            tagged = pos_tag(words, tagset='universal')

            adj_count = 0
            noun_count = 0

            for word, tag in tagged:
                if not self.is_valid_word(word):
                    continue

                if tag == 'ADJ' and word not in self.adjectives:
                    self.adjectives.add(word)
                    adj_count += 1
                elif tag == 'NOUN' and word not in self.nouns:
                    self.nouns.add(word)
                    noun_count += 1

            print(f"  Found {adj_count} new adjectives, {noun_count} new nouns", file=sys.stderr)

        except Exception as e:
            print(f"  Warning: Download failed: {e}", file=sys.stderr)

    def download_word_frequency_list(self):
        """Download and use frequency-ranked word list for better UX."""
        print("Downloading word frequency list (common words only)...", file=sys.stderr)

        # Use hermitdave's frequency word list (based on subtitles corpus)
        # This is cleaner than raw dictionaries and already lowercase
        url = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/en/en_50k.txt"

        try:
            response = urllib.request.urlopen(url, timeout=30)
            lines = response.read().decode('utf-8').splitlines()

            # Format: "word frequency" (space-separated)
            words_with_freq = []
            for line in lines:
                parts = line.strip().split()
                if len(parts) >= 2:
                    word = parts[0].lower()  # Ensure lowercase
                    try:
                        freq = int(parts[1])
                        # is_valid_word checks capitalization and everything else
                        if self.is_valid_word(word):
                            words_with_freq.append((word, freq))
                    except ValueError:
                        continue

            print(f"  Downloaded {len(words_with_freq)} frequency-ranked words", file=sys.stderr)

            # POS tag in batches
            print(f"  POS tagging frequency list...", file=sys.stderr)
            words_only = [w for w, f in words_with_freq]

            batch_size = 1000
            adj_count = 0
            noun_count = 0

            for i in range(0, len(words_only), batch_size):
                batch = words_only[i:i+batch_size]
                tagged = pos_tag(batch)

                for word, tag in tagged:
                    word_lower = word.lower()

                    # Skip proper nouns detected by POS tagger
                    if tag in ('NNP', 'NNPS'):
                        continue

                    # JJ, JJR, JJS = adjectives (base, comparative, superlative)
                    if tag in ('JJ', 'JJR', 'JJS') and word_lower not in self.adjectives:
                        self.adjectives.add(word_lower)
                        adj_count += 1
                    # NN, NNS = common nouns (singular, plural)
                    # Explicitly exclude NNP, NNPS (proper nouns)
                    elif tag in ('NN', 'NNS') and word_lower not in self.nouns:
                        self.nouns.add(word_lower)
                        noun_count += 1

                if (i // batch_size) % 5 == 0:
                    print(f"    Progress: {i}/{len(words_only)} words...", file=sys.stderr)

            print(f"  Found {adj_count} new adjectives, {noun_count} new nouns", file=sys.stderr)

        except Exception as e:
            print(f"  Warning: Download failed: {e}", file=sys.stderr)

    def generate_wordnet_frequency_hybrid(self, target_adjectives=2500, target_nouns=5000):
        """
        Hybrid approach: WordNet vocabulary filtered by word frequency.

        1. Extract all words from WordNet (no proper nouns)
        2. Download frequency rankings
        3. Only keep WordNet words that are in top 50k most frequent
        4. Filter out technical jargon
        5. Sort by frequency and take top N
        """
        print("=" * 60, file=sys.stderr)
        print("Generating wordlists using WordNet + frequency filtering", file=sys.stderr)
        print(f"Target: {target_adjectives} adjectives, {target_nouns} nouns", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

        # Step 1: Extract from WordNet
        print("Extracting from WordNet...", file=sys.stderr)
        wordnet_adj = {}
        wordnet_nouns = {}

        for synset in wordnet.all_synsets('a'):
            for lemma in synset.lemmas():
                word = lemma.name().lower().replace('_', '')
                if ' ' not in word and self.is_valid_word(word):
                    wordnet_adj[word] = 0  # Will fill in frequency later

        for synset in wordnet.all_synsets('n'):
            for lemma in synset.lemmas():
                word = lemma.name().lower().replace('_', '')
                if ' ' not in word and self.is_valid_word(word):
                    wordnet_nouns[word] = 0

        print(f"  WordNet: {len(wordnet_adj)} adj candidates, {len(wordnet_nouns)} noun candidates", file=sys.stderr)

        # Step 2: Download frequency data
        print("Downloading frequency rankings...", file=sys.stderr)
        url = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/en/en_full.txt"

        try:
            response = urllib.request.urlopen(url, timeout=30)
            lines = response.read().decode('utf-8').splitlines()

            freq_dict = {}
            for line in lines[:100000]:  # Only top 100k
                parts = line.strip().split()
                if len(parts) >= 2:
                    word = parts[0].lower()
                    try:
                        freq = int(parts[1])
                        freq_dict[word] = freq
                    except ValueError:
                        continue

            print(f"  Loaded {len(freq_dict)} frequency rankings", file=sys.stderr)

            # Step 3: Match WordNet words with frequency data
            print("Matching WordNet words with frequency data...", file=sys.stderr)
            for word in list(wordnet_adj.keys()):
                if word in freq_dict:
                    wordnet_adj[word] = freq_dict[word]
                else:
                    # Remove words not in frequency list (too rare)
                    del wordnet_adj[word]

            for word in list(wordnet_nouns.keys()):
                if word in freq_dict:
                    wordnet_nouns[word] = freq_dict[word]
                else:
                    del wordnet_nouns[word]

            print(f"  After frequency filter: {len(wordnet_adj)} adj, {len(wordnet_nouns)} nouns", file=sys.stderr)

            # Step 4: Filter out technical jargon
            print("Filtering technical jargon...", file=sys.stderr)
            jargon_suffixes = ('idae', 'aceae', 'ology', 'osis', 'itis', 'ectomy', 'otomy',
                             'scopy', 'plasty', 'gram', 'mycota', 'phyta', 'mycetes')

            for word in list(wordnet_adj.keys()):
                if any(word.endswith(suffix) for suffix in jargon_suffixes):
                    del wordnet_adj[word]

            for word in list(wordnet_nouns.keys()):
                if any(word.endswith(suffix) for suffix in jargon_suffixes):
                    del wordnet_nouns[word]

            print(f"  After jargon filter: {len(wordnet_adj)} adj, {len(wordnet_nouns)} nouns", file=sys.stderr)

            # Step 5: Sort by frequency and take top N
            print("Selecting most frequent words...", file=sys.stderr)
            sorted_adj = sorted(wordnet_adj.items(), key=lambda x: x[1], reverse=True)
            sorted_nouns = sorted(wordnet_nouns.items(), key=lambda x: x[1], reverse=True)

            self.adjectives = {word for word, freq in sorted_adj[:target_adjectives]}
            self.nouns = {word for word, freq in sorted_nouns[:target_nouns]}

            print(f"  Final: {len(self.adjectives)} adjectives, {len(self.nouns)} nouns", file=sys.stderr)

        except Exception as e:
            print(f"  Error: {e}", file=sys.stderr)
            print(f"  Falling back to WordNet only...", file=sys.stderr)
            self.extract_from_wordnet()

        print("=" * 60, file=sys.stderr)
        print(f"TOTAL: {len(self.adjectives)} adjectives, {len(self.nouns)} nouns", file=sys.stderr)
        print(f"Combinations: {len(self.adjectives)} × {len(self.nouns)}² = {len(self.adjectives) * len(self.nouns) * len(self.nouns):,}", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

    def generate_frequency_based(self, target_adjectives=2500, target_nouns=5000):
        """
        Generate wordlists using frequency ranking for high UX quality.

        Only takes the most common words to avoid obscure technical jargon.
        """
        print("=" * 60, file=sys.stderr)
        print("Generating wordlists using frequency ranking", file=sys.stderr)
        print(f"Target: {target_adjectives} adjectives, {target_nouns} nouns", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

        # Download frequency-ranked word list
        print("Downloading word frequency data...", file=sys.stderr)
        url = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/en/en_full.txt"

        try:
            response = urllib.request.urlopen(url, timeout=30)
            lines = response.read().decode('utf-8').splitlines()

            words_by_freq = []
            for line in lines:
                parts = line.strip().split()
                if len(parts) >= 2:
                    word = parts[0]  # Keep original case for validation
                    try:
                        freq = int(parts[1])
                        # is_valid_word checks capitalization
                        if self.is_valid_word(word):
                            words_by_freq.append((word.lower(), freq))  # Store lowercase after validation
                    except ValueError:
                        continue

            print(f"  Downloaded {len(words_by_freq)} valid words with frequency data", file=sys.stderr)

            # Sort by frequency (descending)
            words_by_freq.sort(key=lambda x: x[1], reverse=True)

            # POS tag in batches and extract until we hit targets
            print("  POS tagging most frequent words...", file=sys.stderr)
            batch_size = 1000

            for i in range(0, len(words_by_freq), batch_size):
                # Stop early if we've hit both targets
                if len(self.adjectives) >= target_adjectives and len(self.nouns) >= target_nouns:
                    break

                batch_words = [w for w, f in words_by_freq[i:i+batch_size]]
                tagged = pos_tag(batch_words)

                for word, tag in tagged:
                    word_lower = word.lower()

                    # Skip proper nouns
                    if tag in ('NNP', 'NNPS'):
                        continue

                    # Adjectives
                    if tag in ('JJ', 'JJR', 'JJS') and len(self.adjectives) < target_adjectives:
                        if word_lower not in self.adjectives:
                            self.adjectives.add(word_lower)

                    # Nouns
                    elif tag in ('NN', 'NNS') and len(self.nouns) < target_nouns:
                        if word_lower not in self.nouns:
                            self.nouns.add(word_lower)

                if i % 5000 == 0:
                    print(f"    Progress: {len(self.adjectives)}/{target_adjectives} adj, "
                          f"{len(self.nouns)}/{target_nouns} nouns", file=sys.stderr)

            print(f"  Final: {len(self.adjectives)} adjectives, {len(self.nouns)} nouns", file=sys.stderr)

        except Exception as e:
            print(f"  Error downloading frequency data: {e}", file=sys.stderr)
            print(f"  Falling back to Brown corpus...", file=sys.stderr)
            self.extract_from_nltk_brown()

        print("=" * 60, file=sys.stderr)
        print(f"TOTAL: {len(self.adjectives)} adjectives, {len(self.nouns)} nouns", file=sys.stderr)
        print(f"Combinations: {len(self.adjectives)} × {len(self.nouns)}² = {len(self.adjectives) * len(self.nouns) * len(self.nouns):,}", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

    def generate_all(self, use_wordnet=True):
        """Run all extraction methods (legacy - produces too many obscure words)."""
        print("=" * 60, file=sys.stderr)
        print("Generating wordlists for ACDS session strings", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

        if use_wordnet:
            # Method 1: WordNet (BEST - curated dictionary, no proper nouns)
            self.extract_from_wordnet()

            # Method 2: Brown corpus (adds some common words not in WordNet)
            self.extract_from_nltk_brown()
        else:
            # Alternative: Frequency-based extraction (may include proper nouns)
            # Method 1: NLTK Brown corpus
            self.extract_from_nltk_brown()

            # Method 2: Word frequency list
            self.download_word_frequency_list()

        print("=" * 60, file=sys.stderr)
        print(f"TOTAL: {len(self.adjectives)} adjectives, {len(self.nouns)} nouns", file=sys.stderr)
        print(f"Combinations: {len(self.adjectives)} × {len(self.nouns)}² = {len(self.adjectives) * len(self.nouns) * len(self.nouns):,}", file=sys.stderr)
        print("=" * 60, file=sys.stderr)

    def export_c_array(self, words: set, var_name: str, output_file: Path):
        """Export word list as C array with corresponding header file."""
        # Sort alphabetically for consistency
        sorted_words = sorted(words)

        # Generate .c file
        with open(output_file, 'w') as f:
            f.write(f"// Auto-generated by generate_wordlists.py\n")
            f.write(f"// Total words: {len(sorted_words)}\n\n")
            f.write(f"#include <stddef.h>\n")
            f.write(f"#include \"{output_file.stem}.h\"\n\n")
            f.write(f"const char *{var_name}[] = {{\n")

            # Write 10 words per line for readability
            for i in range(0, len(sorted_words), 10):
                batch = sorted_words[i:i+10]
                line = "    " + ", ".join(f'"{w}"' for w in batch)
                if i + 10 < len(sorted_words):
                    line += ","
                f.write(line + "\n")

            f.write("};\n\n")
            f.write(f"const size_t {var_name}_count = sizeof({var_name}) / sizeof({var_name}[0]);\n")

        # Generate .h file
        header_file = output_file.with_suffix('.h')
        guard_name = f"{output_file.stem.upper()}_H"

        with open(header_file, 'w') as f:
            f.write(f"// Auto-generated by generate_wordlists.py\n")
            f.write(f"// Total words: {len(sorted_words)}\n\n")
            f.write(f"#ifndef {guard_name}\n")
            f.write(f"#define {guard_name}\n\n")
            f.write(f"#include <stddef.h>\n\n")
            f.write(f"// Array of {len(sorted_words)} {var_name}\n")
            f.write(f"extern const char *{var_name}[];\n\n")
            f.write(f"// Number of words in {var_name} array\n")
            f.write(f"extern const size_t {var_name}_count;\n\n")
            f.write(f"#endif // {guard_name}\n")

    def export_text(self, words: set, output_file: Path):
        """Export word list as plain text (one per line)."""
        sorted_words = sorted(words)

        with open(output_file, 'w') as f:
            f.write(f"# Auto-generated by generate_wordlists.py\n")
            f.write(f"# Total words: {len(sorted_words)}\n")
            for word in sorted_words:
                f.write(f"{word}\n")


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description='Generate wordlists for ACDS session strings')
    parser.add_argument('--output-dir', type=Path, default=Path('lib/discovery'),
                        help='Output directory for wordlists')
    parser.add_argument('--format', choices=['c', 'txt', 'both'], default='both',
                        help='Output format')
    parser.add_argument('--min-length', type=int, default=3,
                        help='Minimum word length')
    parser.add_argument('--max-length', type=int, default=12,
                        help='Maximum word length')
    parser.add_argument('--target-adjectives', type=int, default=2500,
                        help='Target number of adjectives (default: 2500)')
    parser.add_argument('--target-nouns', type=int, default=5000,
                        help='Target number of nouns (default: 5000)')

    args = parser.parse_args()

    # Create output directory
    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Generate wordlists using WordNet + frequency hybrid
    generator = WordlistGenerator(min_length=args.min_length, max_length=args.max_length)
    generator.generate_wordnet_frequency_hybrid(
        target_adjectives=args.target_adjectives,
        target_nouns=args.target_nouns
    )

    # Export in requested format
    if args.format in ('txt', 'both'):
        print(f"\nWriting text files to {args.output_dir}/", file=sys.stderr)
        generator.export_text(generator.adjectives, args.output_dir / 'adjectives.txt')
        generator.export_text(generator.nouns, args.output_dir / 'nouns.txt')

    if args.format in ('c', 'both'):
        print(f"\nWriting C arrays to {args.output_dir}/", file=sys.stderr)
        generator.export_c_array(generator.adjectives, 'adjectives', args.output_dir / 'adjectives.c')
        generator.export_c_array(generator.nouns, 'nouns', args.output_dir / 'nouns.c')

    print("\n✅ Done!", file=sys.stderr)
    print(f"\nFinal stats:", file=sys.stderr)
    print(f"  Adjectives: {len(generator.adjectives)}", file=sys.stderr)
    print(f"  Nouns: {len(generator.nouns)}", file=sys.stderr)
    print(f"  Combinations: {len(generator.adjectives) * len(generator.nouns) ** 2:,}", file=sys.stderr)


if __name__ == '__main__':
    main()
