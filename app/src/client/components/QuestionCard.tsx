import { LIMITS, utf8Length, type Issue, type PackQuestion } from '../../shared/pack.ts';
import { ANSWER_SHAPES } from '../theme.ts';

const TIMES = [5, 10, 15, 20, 30, 45, 60];

interface Props {
  index: number;
  count: number;
  question: PackQuestion;
  issues: Issue[];
  onChange: (q: PackQuestion) => void;
  onMove: (delta: -1 | 1) => void;
  onRemove: () => void;
}

function Counter({ text, max }: { text: string; max: number }) {
  const n = utf8Length(text.trim());
  const state = n > max ? ' over' : n > max * 0.8 ? ' near' : '';
  return <span className={`counter${state}`}>{n}/{max}</span>;
}

function Messages({ issues }: { issues: Issue[] }) {
  return (
    <>
      {issues.map((i, k) => <p key={k} className="error small">{i.message}</p>)}
    </>
  );
}

export function QuestionCard({ index, count, question: q, issues, onChange, onMove, onRemove }: Props) {
  const issuesFor = (field: string, answer?: number) =>
    issues.filter((i) => i.path.field === field && (answer === undefined || i.path.answer === answer));

  const setAnswer = (j: number, text: string) =>
    onChange({ ...q, answers: q.answers.map((a, k) => (k === j ? text : a)) });

  const removeAnswer = (j: number) => {
    const answers = q.answers.filter((_, k) => k !== j);
    const correct = q.correct === j ? 0 : q.correct > j ? q.correct - 1 : q.correct;
    onChange({ ...q, answers, correct });
  };

  const time = q.time ?? LIMITS.defaultTime;

  return (
    <div className="card question-card">
      <div className="question-head">
        <h3>Vraag {index + 1}</h3>
        <div className="actions">
          <select
            aria-label="Tijd"
            value={time}
            onChange={(e) => onChange({ ...q, time: Number(e.target.value) })}
          >
            {[...new Set([...TIMES, time])].sort((a, b) => a - b).map((t) => (
              <option key={t} value={t}>{t} s</option>
            ))}
          </select>
          <button className="icon" aria-label="Omhoog" disabled={index === 0} onClick={() => onMove(-1)}>↑</button>
          <button className="icon" aria-label="Omlaag" disabled={index === count - 1} onClick={() => onMove(1)}>↓</button>
          <button className="icon danger" aria-label="Vraag verwijderen" disabled={count === 1} onClick={onRemove}>×</button>
        </div>
      </div>

      <div className="field">
        <textarea
          rows={2}
          placeholder="Stel een vraag"
          value={q.question}
          onChange={(e) => onChange({ ...q, question: e.target.value })}
        />
        <Counter text={q.question} max={LIMITS.questionBytes} />
      </div>
      <Messages issues={issuesFor('question')} />

      <div className="answers">
        {q.answers.map((a, j) => (
          <div key={j} className={`answer a${j}${q.correct === j ? ' correct' : ''}`}>
            <span className={`shape ${ANSWER_SHAPES[j]}`} aria-hidden="true" />
            <input
              placeholder={`Antwoord ${j + 1}`}
              value={a}
              onChange={(e) => setAnswer(j, e.target.value)}
            />
            <Counter text={a} max={LIMITS.answerBytes} />
            <label className="correct-toggle" title="Goede antwoord">
              <input
                type="radio"
                name={`correct-${index}`}
                checked={q.correct === j}
                onChange={() => onChange({ ...q, correct: j })}
              />
              <span>goed</span>
            </label>
            {q.answers.length > LIMITS.minAnswers && (
              <button className="icon" aria-label={`Antwoord ${j + 1} verwijderen`} onClick={() => removeAnswer(j)}>×</button>
            )}
            <Messages issues={issuesFor('answer', j)} />
          </div>
        ))}
      </div>
      {q.answers.length < LIMITS.maxAnswers && (
        <button className="ghost add-answer" onClick={() => onChange({ ...q, answers: [...q.answers, ''] })}>
          + Antwoord
        </button>
      )}
      <Messages issues={[...issuesFor('answers'), ...issuesFor('correct'), ...issuesFor('time')]} />
    </div>
  );
}
